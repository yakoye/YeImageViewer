#pragma once

// 离屏诊断：不显示窗口、不置前台、不注入输入。用于区分GDI批处理/字体缓存与HUD生命周期。
inline int runGdiBatchProbe(bool flush) {
    bool okay=true;
    const auto baseline=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int cycle=0;cycle<30;++cycle){
        std::thread worker([&]{
            HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;
            BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),300,-400,1,32,BI_RGB};
            HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            HFONT font=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            if(!dc||!bitmap||!font){okay=false;if(font)DeleteObject(font);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return;}
            const auto oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font);
            RECT rect{0,0,300,400};DrawTextW(dc,L"取色 坐标 HEX RGB HSL HSV/HSB CMYK HSI CIE-LAB\nShift：切换颜色格式\n左键/Enter：复制退出\nEsc/右键：取消",-1,&rect,DT_WORDBREAK|DT_NOPREFIX);
            SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);
            const bool fontDeleted=DeleteObject(font)!=FALSE,bitmapDeleted=DeleteObject(bitmap)!=FALSE,dcDeleted=DeleteDC(dc)!=FALSE;
            okay=okay&&fontDeleted&&bitmapDeleted&&dcDeleted;
            if(flush)GdiFlush();
        });worker.join();
        std::cout<<"GDI-OFFSCREEN flush="<<flush<<" cycle="<<cycle<<" baseline="<<baseline<<" now="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<" deletes="<<okay<<"\n";
    }
    return okay?0:2;
}
