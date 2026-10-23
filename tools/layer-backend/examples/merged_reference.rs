// 离屏诊断：通过独立PhotoCraft解析器读取文件已存合成图，不重新合成原图层。
// 仅写入调用方明确指定的诊断BMP，绝不保存输入PSD。
use photocraft_psd::PsdFile;
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = std::env::args().skip(1);
    let input = args.next().ok_or("缺少输入文件")?;
    let output = args.next().ok_or("缺少输出文件")?;
    let mut psd = PsdFile::from_bytes(&std::fs::read(input)?)?;
    if u64::from(psd.header.width) * u64::from(psd.header.height) > 16 * 1024 * 1024 {
        return Err("诊断尺寸上限".into());
    }
    psd.layer_info = None;
    psd.global_blocks
        .retain(|b| !matches!(&b.key, b"Lr16" | b"Lr32" | b"Layr"));
    let (doc, warnings) = photocraft_io::psd_to_document(&psd);
    let image = photocraft_compose::render(&doc, doc.bounds()).to_rgba8();
    let length = image.pixels.len();
    let mut bytes = Vec::with_capacity(length + 54);
    bytes.extend_from_slice(b"BM");
    bytes.extend_from_slice(&((length + 54) as u32).to_le_bytes());
    bytes.extend_from_slice(&[0; 4]);
    bytes.extend_from_slice(&54u32.to_le_bytes());
    bytes.extend_from_slice(&40u32.to_le_bytes());
    bytes.extend_from_slice(&(doc.size.width as i32).to_le_bytes());
    bytes.extend_from_slice(&(-(doc.size.height as i32)).to_le_bytes());
    bytes.extend_from_slice(&1u16.to_le_bytes());
    bytes.extend_from_slice(&32u16.to_le_bytes());
    bytes.extend_from_slice(&[0; 24]);
    for p in image.pixels.chunks_exact(4) {
        bytes.extend_from_slice(&[p[2], p[1], p[0], p[3]]);
    }
    std::fs::write(output, bytes)?;
    println!(
        "{}x{} warnings={warnings:?}",
        doc.size.width, doc.size.height
    );
    Ok(())
}
