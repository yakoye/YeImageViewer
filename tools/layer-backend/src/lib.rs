//! YeImageViewer 的只读图层后端。直接链接固定版本 PhotoCraft 的 io/doc/compose。
//! C ABI 不传 Rust 容器；句柄、返回像素均由本模块释放。调用方必须串行使用每个句柄。
#![allow(non_snake_case)]
use photocraft_color::{BlendMode, PixelFormat, SampleType};
use photocraft_doc::{Document, Layer, LayerContent};
use photocraft_psd::PsdFile;
use std::{
    panic::{AssertUnwindSafe, catch_unwind},
    ptr, slice,
};

const MAX_FILE: usize = 256 << 20;
const MAX_PIXELS: u64 = 16 * 1024 * 1024;
const MAX_LAYERS: usize = 10000;
type Cancel = Option<extern "C" fn(*mut std::ffi::c_void) -> u32>;
fn cancelled(cb: Cancel, context: usize) -> bool {
    cb.is_some_and(|f| f(context as *mut std::ffi::c_void) != 0)
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct Info {
    pub width: u32,
    pub height: u32,
    pub depth: u32,
    pub mode: u32,
    pub layers: u32,
    pub channels: u32,
    pub warning_bytes: u32,
    pub reserved: u32,
}
#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct Row {
    pub id: u64,
    pub section: u32,
    pub flags: u32,
    pub opacity: f32,
    pub fill: f32,
    pub blend: [u8; 4],
    pub name_bytes: u32,
}
#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct State {
    pub visible: u32,
    pub mask: u32,
    pub opacity: f32,
    pub fill: f32,
    pub blend: [u8; 4],
}
#[repr(C)]
#[derive(Default)]
pub struct Pixels {
    pub data: *mut u8,
    pub length: usize,
    pub width: u32,
    pub height: u32,
}

struct Record {
    row: Row,
    name: Vec<u8>,
}
pub struct Handle {
    doc: Document,
    info: Info,
    records: Vec<Record>,
    warnings: Vec<u8>,
}

// 0 成功；1 数据/参数错误；2 安全限额；3 取消；4 后端内部异常。
fn guard(f: impl FnOnce() -> Result<(), u32>) -> u32 {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(())) => 0,
        Ok(Err(e)) => e,
        Err(_) => 4,
    }
}
fn records(layers: &[Layer], out: &mut Vec<Record>, depth: usize) -> Result<(), u32> {
    if depth > 64 {
        return Err(2);
    }
    for l in layers.iter().rev() {
        let group = matches!(l.content, LayerContent::Group(_));
        let flags = u32::from(l.visible)
            | (u32::from(l.mask.is_some()) << 1)
            | (u32::from(l.mask.as_ref().is_some_and(|m| m.enabled)) << 2)
            | (u32::from(l.vector_mask.is_some()) << 3)
            | (u32::from(l.clipped) << 4)
            | (u32::from(l.locks.any()) << 5)
            | (u32::from(matches!(l.content, LayerContent::Adjustment(_))) << 6)
            | (u32::from(!l.effects.items.is_empty()) << 7);
        if l.name.len() > 65536 || out.len() >= MAX_LAYERS {
            return Err(2);
        }
        out.push(Record {
            row: Row {
                id: l.id.0,
                section: if group { 1 } else { 0 },
                flags,
                opacity: l.opacity,
                fill: l.fill_opacity,
                blend: l.blend.psd_key(),
                name_bytes: l.name.len() as u32,
            },
            name: l.name.as_bytes().to_vec(),
        });
        if let LayerContent::Group(g) = &l.content {
            if !g.expanded {
                out.last_mut().ok_or(1u32)?.row.section = 2;
            }
            records(&g.children, out, depth + 1)?;
            if out.len() >= MAX_LAYERS {
                return Err(2);
            }
            out.push(Record {
                row: Row {
                    section: 3,
                    ..Row::default()
                },
                name: Vec::new(),
            });
        }
    }
    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn yl_version() -> u32 {
    1
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_open(
    data: *const u8,
    len: usize,
    out: *mut *mut Handle,
    cb: Cancel,
    context: *mut std::ffi::c_void,
) -> u32 {
    if out.is_null() {
        return 1;
    }
    unsafe {
        *out = ptr::null_mut();
    }
    guard(|| {
        if data.is_null() || len < 26 {
            return Err(1);
        }
        if len > MAX_FILE {
            return Err(2);
        }
        let bytes = unsafe { slice::from_raw_parts(data, len) };
        let width = u32::from_be_bytes(bytes[18..22].try_into().map_err(|_| 1u32)?);
        let height = u32::from_be_bytes(bytes[14..18].try_into().map_err(|_| 1u32)?);
        if width == 0 || height == 0 || u64::from(width) * u64::from(height) > MAX_PIXELS {
            return Err(2);
        }
        let ctx = context as usize;
        if cancelled(cb, ctx) {
            return Err(3);
        }
        let (psd, _) = PsdFile::from_bytes_and_merged_recovering(
            bytes,
            |_| false,
            |file| {
                matches!(
                    file.header.color_mode,
                    photocraft_psd::ColorMode::Rgb
                        | photocraft_psd::ColorMode::Cmyk
                        | photocraft_psd::ColorMode::Grayscale
                        | photocraft_psd::ColorMode::Lab
                ) && file.header.depth != 1
                    && !file.layers().is_empty()
            },
        )
        .map_err(|_| 1u32)?;
        if psd.layers().len() > MAX_LAYERS {
            return Err(2);
        }
        let decoded = psd
            .layers()
            .iter()
            .try_fold(0u64, |sum, l| {
                let area = u64::try_from(l.rect.width())
                    .ok()?
                    .saturating_mul(u64::try_from(l.rect.height()).ok()?);
                let amount = area
                    .saturating_mul(u64::from(psd.header.depth) / 8)
                    .saturating_mul(l.channels.len() as u64);
                sum.checked_add(amount)
            })
            .ok_or(2u32)?;
        if decoded > 1024 * 1024 * 1024 {
            return Err(2);
        }
        let depth = u32::from(psd.header.depth);
        let mode = u32::from(u16::from_be_bytes([bytes[24], bytes[25]]));
        drop(psd);
        let check = || cancelled(cb, ctx);
        // 复用应用同一导入入口（含损坏合成图恢复、导入警告及文字缓存准备），
        // 不能仅调用格式层psd_to_document后遗漏上层语义。
        let imported = photocraft_io::import_psd(
            "document.psd",
            bytes,
            &photocraft_raster::Interrupt::cancel_only(&check),
        )
        .map_err(|e| {
            if matches!(e, photocraft_io::IoError::Cancelled) {
                3u32
            } else {
                1u32
            }
        })?;
        let (doc, warnings) = (imported.document, imported.warnings);
        let mut rows = Vec::new();
        records(&doc.layers, &mut rows, 0)?;
        let warnings = warnings.join("\n").into_bytes();
        if warnings.len() > 1 << 20 {
            return Err(2);
        }
        let info = Info {
            width,
            height,
            depth,
            mode,
            layers: rows.len() as u32,
            channels: doc.channels.len() as u32,
            warning_bytes: warnings.len() as u32,
            reserved: 0,
        };
        unsafe {
            *out = Box::into_raw(Box::new(Handle {
                doc,
                info,
                records: rows,
                warnings,
            }));
        }
        Ok(())
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_close(handle: *mut Handle) {
    if !handle.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
            drop(Box::from_raw(handle));
        }));
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_info(handle: *const Handle, out: *mut Info) -> u32 {
    guard(|| {
        if handle.is_null() || out.is_null() {
            return Err(1);
        }
        unsafe {
            *out = (*handle).info;
        }
        Ok(())
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_row(
    handle: *const Handle,
    index: usize,
    out: *mut Row,
    name: *mut u8,
    capacity: usize,
) -> u32 {
    guard(|| {
        if handle.is_null() || out.is_null() {
            return Err(1);
        }
        let rec = unsafe { &*handle }.records.get(index).ok_or(1u32)?;
        unsafe {
            *out = rec.row;
        }
        if !name.is_null() {
            if capacity < rec.name.len() {
                return Err(1);
            }
            unsafe {
                ptr::copy_nonoverlapping(rec.name.as_ptr(), name, rec.name.len());
            }
        }
        Ok(())
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_warnings(handle: *const Handle, out: *mut u8, capacity: usize) -> u32 {
    guard(|| {
        if handle.is_null() {
            return Err(1);
        }
        let data = &unsafe { &*handle }.warnings;
        if data.is_empty() {
            return Ok(());
        }
        if out.is_null() || capacity < data.len() {
            return Err(1);
        }
        unsafe {
            ptr::copy_nonoverlapping(data.as_ptr(), out, data.len());
        }
        Ok(())
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_channel_name(
    handle: *const Handle,
    index: usize,
    out: *mut u8,
    capacity: usize,
    length: *mut usize,
) -> u32 {
    guard(|| {
        if handle.is_null() || length.is_null() {
            return Err(1);
        }
        let c = unsafe { &*handle }.doc.channels.get(index).ok_or(1u32)?;
        unsafe {
            *length = c.name.len();
        }
        if !out.is_null() {
            if capacity < c.name.len() {
                return Err(1);
            }
            unsafe {
                ptr::copy_nonoverlapping(c.name.as_ptr(), out, c.name.len());
            }
        }
        Ok(())
    })
}

// states 覆盖整个文档（包括目录结束行）；每次从原始 COW 文档开始，状态不残留。
// 移植上游 ui-egui/src/lib.rs 的 layer_thumb/mask_thumb/thumb_image（固定提交，MIT）。
// 图层列表展示原始层像素和独立蒙版，不为每一行重合成整张画布；主图仍走yl_render。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_thumbnail(handle: *const Handle,index: usize,kind: u32,side: u32,out: *mut Pixels,cb: Cancel,context: *mut std::ffi::c_void) -> u32 {
    if out.is_null(){return 1;}
    unsafe{*out=Pixels::default();}
    guard(||{
        if handle.is_null()||side==0||side>128||kind>1{return Err(1);}
        let h=unsafe{&*handle};let row=h.records.get(index).ok_or(1u32)?;
        if row.row.section==3{return Err(1);}
        let layer=h.doc.layer(photocraft_doc::LayerId(row.row.id)).ok_or(1u32)?;
        let surface=if kind==1{&layer.mask.as_ref().ok_or(1u32)?.surface}else{layer.surface().ok_or(1u32)?};
        let frame=h.doc.bounds();let (w,height)=(frame.width().max(1) as f32,frame.height().max(1) as f32);
        let scale=w.max(height)/side as f32;let (ox,oy)=((side as f32-w/scale)/2.,(side as f32-height/scale)/2.);
        let mut bytes=vec![0u8;side as usize*side as usize*4];let mut px=[0f32;8];
        for ty in 0..side{
            if cancelled(cb,context as usize){return Err(3);}
            for tx in 0..side{
                let dx=(tx as f32-ox+0.5)*scale;let dy=(ty as f32-oy+0.5)*scale;
                if dx<0.||dy<0.||dx>=w||dy>=height{continue;}
                let n=surface.channels();if n>px.len(){return Err(1);}
                surface.read_pixel(frame.x0.saturating_add(dx as i32),frame.y0.saturating_add(dy as i32),&mut px[..n]);
                let c=if kind==1{[px[0],px[0],px[0],1.]}else{photocraft_raster::to_rgba(&surface.format(),&px[..n])};
                let q=c.map(|v|(v.clamp(0.,1.)*255.+0.5) as u8);let p=((ty*side+tx)*4) as usize;
                bytes[p..p+4].copy_from_slice(&[q[2],q[1],q[0],q[3]]);
            }
        }
        let boxed=bytes.into_boxed_slice();let length=boxed.len();let data=Box::into_raw(boxed) as *mut u8;
        unsafe{*out=Pixels{data,length,width:side,height:side};}Ok(())
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_render(
    handle: *const Handle,
    states: *const State,
    count: usize,
    solo: i32,
    channel: i32,
    out: *mut Pixels,
    cb: Cancel,
    context: *mut std::ffi::c_void,
) -> u32 {
    if out.is_null() {
        return 1;
    }
    unsafe {
        *out = Pixels::default();
    }
    guard(|| {
        if handle.is_null() || states.is_null() {
            return Err(1);
        }
        let h = unsafe { &*handle };
        if count != h.records.len() || solo < -1 || solo >= count as i32 || channel < 0 {
            return Err(1);
        }
        let states = unsafe { slice::from_raw_parts(states, count) };
        let ctx = context as usize;
        if cancelled(cb, ctx) {
            return Err(3);
        }
        let mut doc = h.doc.clone();
        for (r, s) in h.records.iter().zip(states) {
            if r.row.section == 3 {
                continue;
            }
            if s.visible > 1
                || s.mask > 1
                || !s.opacity.is_finite()
                || !s.fill.is_finite()
                || !(0.0..=1.0).contains(&s.opacity)
                || !(0.0..=1.0).contains(&s.fill)
            {
                return Err(1);
            }
            let mode = BlendMode::from_psd_key(s.blend).ok_or(1u32)?;
            let l = doc
                .layer_mut(photocraft_doc::LayerId(r.row.id))
                .ok_or(1u32)?;
            l.visible = s.visible != 0;
            l.opacity = s.opacity;
            l.fill_opacity = s.fill;
            l.blend = mode;
            if let Some(mask) = l.mask.as_mut() {
                mask.enabled = s.mask != 0;
            }
        }
        let rect = doc.bounds();
        let mut bytes = Vec::with_capacity(doc.size.width as usize * doc.size.height as usize * 4);
        if channel >= 20000 {
            let r = h.records.get((channel - 20000) as usize).ok_or(1u32)?;
            let l = doc.layer(photocraft_doc::LayerId(r.row.id)).ok_or(1u32)?;
            let mask = l.mask.as_ref().ok_or(1u32)?;
            for y in rect.y0..rect.y1 {
                if cancelled(cb, ctx) {
                    return Err(3);
                }
                for x in rect.x0..rect.x1 {
                    let v =
                        (mask.surface.sample_channel(x, y, 0).clamp(0.0, 1.0) * 255.0 + 0.5) as u8;
                    bytes.extend_from_slice(&[v, v, v, 255]);
                }
            }
        } else if channel >= 100 {
            let c = doc.channels.get((channel - 100) as usize).ok_or(1u32)?;
            for y in rect.y0..rect.y1 {
                if cancelled(cb, ctx) {
                    return Err(3);
                }
                for x in rect.x0..rect.x1 {
                    let v = (c.surface.sample_channel(x, y, 0).clamp(0.0, 1.0) * 255.0 + 0.5) as u8;
                    bytes.extend_from_slice(&[v, v, v, 255]);
                }
            }
        } else if channel >= 10 {
            // 原生模式处理色版：CMYK/Lab 不冒充转换后的 RGB 分量。
            let component = (channel - 10) as usize;
            if component >= doc.mode.color_channels() {
                return Err(1);
            }
            let fmt = PixelFormat {
                mode: doc.mode,
                sample: SampleType::F32,
                alpha: true,
            };
            let native = photocraft_compose::flatten_to_surface(&doc, fmt, None);
            for y in rect.y0..rect.y1 {
                if cancelled(cb, ctx) {
                    return Err(3);
                }
                for x in rect.x0..rect.x1 {
                    let mut v = native.sample_channel(x, y, component).clamp(0.0, 1.0);
                    if doc.mode == photocraft_color::ColorMode::Cmyk {
                        v = 1.0 - v;
                    }
                    let q = (v * 255.0 + 0.5) as u8;
                    bytes.extend_from_slice(&[q, q, q, 255]);
                }
            }
        } else {
            if channel > 4 {
                return Err(1);
            }
            let append = |bytes: &mut Vec<u8>, image: photocraft_compose::Buffer| {
                for p in image.px {
                    let q = p.map(|v| (v.clamp(0.0, 1.0) * 255.0 + 0.5) as u8);
                    if channel == 0 {
                        bytes.extend_from_slice(&[q[2], q[1], q[0], q[3]]);
                    } else {
                        let v = q[(channel - 1) as usize];
                        bytes.extend_from_slice(&[v, v, v, 255]);
                    }
                }
            };
            if solo >= 0 {
                let r = h.records.get(solo as usize).ok_or(1u32)?;
                let l = doc.layer(photocraft_doc::LayerId(r.row.id)).ok_or(1u32)?;
                append(
                    &mut bytes,
                    photocraft_compose::render_layer_in(&doc, l, rect),
                );
            } else {
                photocraft_compose::render_bands(&doc, rect, 256, |band| {
                    if cancelled(cb, ctx) {
                        return Err(3u32);
                    }
                    append(&mut bytes, band);
                    Ok(())
                })?;
            }
        }
        if cancelled(cb, ctx) {
            return Err(3);
        }
        let boxed = bytes.into_boxed_slice();
        let length = boxed.len();
        let data = Box::into_raw(boxed) as *mut u8;
        unsafe {
            *out = Pixels {
                data,
                length,
                width: h.info.width,
                height: h.info.height,
            };
        }
        Ok(())
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn yl_free_pixels(p: *mut Pixels) {
    if p.is_null() {
        return;
    }
    let p = unsafe { &mut *p };
    if !p.data.is_null() {
        unsafe {
            drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
                p.data, p.length,
            )));
        }
        *p = Pixels::default();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn abi_sizes_and_invalid_inputs() {
        assert_eq!(size_of::<Info>(), 32);
        assert_eq!(size_of::<Row>(), 32);
        assert_eq!(size_of::<State>(), 20);
        assert_eq!(yl_version(), 1);
        let mut h = ptr::null_mut();
        unsafe {
            assert_eq!(yl_open(ptr::null(), 0, &mut h, None, ptr::null_mut()), 1);
            assert!(h.is_null());
        }
        let mut p = Pixels::default();
        unsafe {
            assert_eq!(
                yl_render(h, ptr::null(), 0, -1, 0, &mut p, None, ptr::null_mut()),
                1
            );
            yl_free_pixels(&mut p);
            yl_close(h);
        }
    }
}
