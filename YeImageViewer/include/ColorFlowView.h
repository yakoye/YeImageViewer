#pragma once
#include "ColorFlowModel.h"
#include "ColorCatalog.h"
#include "ContrastRatio.h"
#include "ControlFeedback.h"
#include <windowsx.h>

// 有界绘制的连续色块视口。只有一个HWND；滚动条自行绘制，浅深色一致，无原生白色轨道。
class ColorFlowView {
public:
    static constexpr UINT MESSAGE=WM_APP+75;
    static constexpr UINT QUERY=WM_APP+76;
    enum Query {Count=0,Offset=1,Maximum=2,Selection=3,SelectedRgb=4,Hover=5};
    enum Action {Copy=0,Favorite=1,Context=2,More=3};
private:
    HWND window_=nullptr;int id_=0,dpi_=96,selection_=-1;bool dark_=false,list_=false,dragging_=false,more_=false;
    int dragY=0;double dragOffset=0,offset_=0;
    int hover_=-1,pressed_=-1;bool hoverStar_=false,pressedStar_=false,hoverBar_=false;
    HFONT font_=nullptr;ColorFlow::Layout layout_;std::vector<ColorFlow::Item> items_;
    int s(int n)const{return MulDiv(n,dpi_,96);}
    RECT client()const{RECT r{};GetClientRect(window_,&r);return r;}
    int logicalWidth()const{return MulDiv(client().right,96,dpi_);}
    int logicalHeight()const{return MulDiv(client().bottom,96,dpi_);}
    static void fill(HDC dc,RECT rect,COLORREF color){const auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
    static void swatch(HDC dc,const RECT& r,const ColorFlow::Item& item){
        if(item.stops.empty()){fill(dc,r,RGB(item.color.r,item.color.g,item.color.b));return;}
        const int w=std::max<LONG>(1,r.right-r.left),h=std::max<LONG>(1,r.bottom-r.top);cv::Mat image(h,w,CV_8UC4);ColorCatalog::Gradient gradient;gradient.colors=item.stops;gradient.angle=item.angle;gradient.radial=item.radial;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto c=gradient.at(gradient.position(x,y,w,h));image.at<cv::Vec4b>(y,x)={c.b,c.g,c.r,255};}
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};SetDIBitsToDevice(dc,r.left,r.top,w,h,0,0,0,h,image.data,&info,DIB_RGB_COLORS);
    }
    COLORREF paper()const{return dark_?RGB(24,26,30):RGB(244,246,248);}
    COLORREF ink()const{return dark_?RGB(235,237,243):RGB(35,43,57);}
    RECT bar()const {auto r=client();r.left=std::max<LONG>(0,r.right-s(12));return r;}
    RECT thumb()const {auto r=bar();const int height=r.bottom;const int content=std::max(1,s(layout_.height()));
        const int size=std::min(height,std::max(s(24),height*height/content));
        const int maximum=layout_.maximum(logicalHeight());r.top=maximum?int(std::round(offset_/maximum*(height-size))):0;r.bottom=r.top+size;return r;}
    void notify(Action action,int index){PostMessageW(GetParent(window_),MESSAGE,MAKEWPARAM(id_,action),index);}
    void scrollTo(double value){offset_=std::clamp(value,0.,double(layout_.maximum(logicalHeight())));hover_=-1;hoverStar_=false;InvalidateRect(window_,nullptr,FALSE);
        if(more_&&offset_+logicalHeight()+160>=layout_.height()){more_=false;notify(More,-1);}}
    void relayout(){layout_.rebuild(items_,logicalWidth(),list_);scrollTo(offset_);}
    bool starAt(int index,int x,int y)const{
        if(!list_||index<0)return false;
        for(const auto& e:layout_.elements())if(e.item==index)return x>=s(e.rect.x+e.rect.width-28)&&y<s(e.rect.y-int(offset_)+30);
        return false;
    }
    void hoverAt(int x,int y){
        const bool onBar=x>=bar().left&&layout_.maximum(logicalHeight())>0;
        const int next=onBar?-1:layout_.at(MulDiv(x,96,dpi_),MulDiv(y,96,dpi_),int(offset_));
        const bool star=starAt(next,x,y);
        if(next!=hover_||star!=hoverStar_||onBar!=hoverBar_){hover_=next;hoverStar_=star;hoverBar_=onBar;InvalidateRect(window_,nullptr,FALSE);}
    }
    void paint(HDC output=nullptr){
        PAINTSTRUCT ps{};const RECT viewport=client();const auto screen=output?output:BeginPaint(window_,&ps);if(output)ps.rcPaint=viewport;
        const auto dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,std::max<LONG>(1,viewport.right),std::max<LONG>(1,viewport.bottom));
        if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(!output)EndPaint(window_,&ps);return;}
        const auto oldBitmap=SelectObject(dc,bitmap),oldFont=SelectObject(dc,font_?font_:GetStockObject(DEFAULT_GUI_FONT));
        fill(dc,viewport,paper());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink());
        const auto& elements=layout_.elements();const int offset=int(std::round(offset_));
        for(size_t n=layout_.firstVisible(offset);n<elements.size()&&elements[n].rect.y<offset+logicalHeight();++n) {
            const auto& e=elements[n];RECT r{s(e.rect.x),s(e.rect.y-offset),s(e.rect.x+e.rect.width),s(e.rect.y+e.rect.height-offset)};
            if(e.item<0){r.left+=s(2);DrawTextW(dc,e.heading.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);continue;}
            const auto& item=items_[e.item];const COLORREF color=RGB(item.color.r,item.color.g,item.color.b);
            if(list_) {
                fill(dc,r,ControlFeedback::background(dark_,e.item==selection_,e.item==hover_,e.item==pressed_&&e.item==hover_,false));
                RECT preview{r.left+s(8),r.top+s(10),r.left+s(56),r.bottom-s(10)};swatch(dc,preview,item);
                SetTextColor(dc,ink());RECT text{r.left+s(66),r.top+s(8),r.right-s(28),r.top+s(32)};
                const auto label=item.title.empty()?ColorValue::hex(item.color):item.title;
                DrawTextW(dc,label.c_str(),-1,&text,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
                text.top+=s(28);text.bottom+=s(28);SetTextColor(dc,dark_?RGB(166,174,189):RGB(101,110,126));
                DrawTextW(dc,item.detail.c_str(),-1,&text,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            } else {
                swatch(dc,r,item);
                const auto textColor=ContrastRatio::between(0xFF000000,CurrentColor::pack(item.color)|0xFF000000)>=4.5?RGB(0,0,0):RGB(255,255,255);
                SetTextColor(dc,textColor);RECT text=r;InflateRect(&text,-s(5),-s(4));
                if(item.stops.empty()){
                    const int middle=(text.top+text.bottom)/2;RECT name=text;name.bottom=middle;
                    DrawTextW(dc,item.title.c_str(),-1,&name,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
                    text.top=middle;const auto hex=ColorValue::hex(item.color);
                    DrawTextW(dc,hex.c_str(),-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
                }
                else{RECT caption=r;caption.top=caption.bottom-s(24);fill(dc,caption,dark_?RGB(36,39,44):RGB(255,255,255));SetTextColor(dc,ink());InflateRect(&caption,-s(5),0);DrawTextW(dc,item.title.c_str(),-1,&caption,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);}
            }
            if(list_){RECT star{r.right-s(26),r.top+s(3),r.right-s(2),r.top+s(27)};if(e.item==hover_&&hoverStar_)fill(dc,star,ControlFeedback::background(dark_,false,true,e.item==pressed_,false));SetTextColor(dc,e.item==hover_&&hoverStar_?ControlFeedback::accent(dark_):ink());DrawTextW(dc,item.favorite?L"★":L"☆",-1,&star,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}
            if(e.item==hover_)ControlFeedback::outline(dc,r,e.item==pressed_,s(1));
            if(e.item==selection_&&GetFocus()==window_){RECT focus=r;InflateRect(&focus,-2,-2);DrawFocusRect(dc,&focus);}
        }
        if(items_.empty()){RECT empty=viewport;InflateRect(&empty,-s(12),-s(12));DrawTextW(dc,trW(L"没有匹配的颜色",L"No matching colors",L"沒有符合的色彩"),-1,&empty,DT_CENTER|DT_TOP|DT_WORDBREAK);}
        if(layout_.maximum(logicalHeight())>0){fill(dc,bar(),dark_?RGB(30,33,38):RGB(232,235,239));auto t=thumb();InflateRect(&t,-s(2),0);fill(dc,t,hoverBar_||dragging_?ControlFeedback::accent(dark_):dark_?RGB(101,109,123):RGB(140,149,161));}
        BitBlt(screen,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,dc,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);
        SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);if(!output)EndPaint(window_,&ps);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<ColorFlowView*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<ColorFlowView*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,w,l);
        switch(message){
        case QUERY:switch(w){case Count:return self->items_.size();case Offset:return int(self->offset_);case Maximum:return self->layout_.maximum(self->logicalHeight());case Selection:return self->selection_;case SelectedRgb:return self->selection_>=0?CurrentColor::pack(self->items_[self->selection_].color):-1;case Hover:return self->hover_;}return -1;
        case WM_GETDLGCODE:return DLGC_WANTARROWS|(w==VK_RETURN?DLGC_WANTALLKEYS:0);
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:self->paint();return 0;
        case WM_PRINTCLIENT:self->paint(reinterpret_cast<HDC>(w));return 0;
        case WM_SIZE:self->relayout();return 0;
        case WM_SETFONT:self->font_=reinterpret_cast<HFONT>(w);InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_MOUSEWHEEL:self->scrollTo(self->offset_-GET_WHEEL_DELTA_WPARAM(w)/120.*160);return 0;
        case WM_VSCROLL:{double value=self->offset_;switch(LOWORD(w)){case SB_TOP:value=0;break;case SB_BOTTOM:value=self->layout_.height();break;case SB_LINEUP:value-=32;break;case SB_LINEDOWN:value+=32;break;case SB_PAGEUP:value-=self->logicalHeight();break;case SB_PAGEDOWN:value+=self->logicalHeight();break;}self->scrollTo(value);return 0;}
        case WM_LBUTTONDOWN:{SetFocus(window);const int x=GET_X_LPARAM(l),y=GET_Y_LPARAM(l);auto bar=self->bar();
            if(x>=bar.left&&self->layout_.maximum(self->logicalHeight())>0){const auto thumb=self->thumb();if(y>=thumb.top&&y<thumb.bottom){self->dragging_=true;self->dragY=y;self->dragOffset=self->offset_;SetCapture(window);}else self->scrollTo(self->offset_+(y<thumb.top?-1:1)*self->logicalHeight());return 0;}
            const int index=self->layout_.at(MulDiv(x,96,self->dpi_),MulDiv(y,96,self->dpi_),int(self->offset_));
            if(index>=0){self->selection_=index;self->pressed_=index;self->pressedStar_=self->starAt(index,x,y);self->hoverAt(x,y);SetCapture(window);InvalidateRect(window,nullptr,FALSE);}return 0;}
        case WM_MOUSEMOVE:{self->hoverAt(GET_X_LPARAM(l),GET_Y_LPARAM(l));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);if(self->dragging_){const auto thumb=self->thumb();const int space=self->client().bottom-(thumb.bottom-thumb.top);if(space>0)self->scrollTo(self->dragOffset+double(GET_Y_LPARAM(l)-self->dragY)*self->layout_.maximum(self->logicalHeight())/space);}return 0;}
        case WM_MOUSELEAVE:self->hover_=-1;self->hoverStar_=self->hoverBar_=false;InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_LBUTTONUP:{self->hoverAt(GET_X_LPARAM(l),GET_Y_LPARAM(l));const int pressed=self->pressed_;const bool star=self->pressedStar_;self->pressed_=-1;self->dragging_=false;if(GetCapture()==window)ReleaseCapture();if(pressed>=0&&pressed==self->hover_&&star==self->hoverStar_)self->notify(star?Favorite:Copy,pressed);InvalidateRect(window,nullptr,FALSE);return 0;}
        case WM_CANCELMODE:case WM_CAPTURECHANGED:self->dragging_=false;self->pressed_=-1;InvalidateRect(window,nullptr,FALSE);return 0;
        case WM_SHOWWINDOW:if(!w){self->hover_=-1;self->pressed_=-1;self->hoverBar_=self->hoverStar_=false;self->dragging_=false;}break;
        case WM_CONTEXTMENU:{POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};int index=self->selection_;if(point.x!=-1){ScreenToClient(window,&point);index=self->layout_.at(MulDiv(point.x,96,self->dpi_),MulDiv(point.y,96,self->dpi_),int(self->offset_));}
            if(index>=0){self->selection_=index;self->notify(Context,index);}return 0;}
        case WM_KEYDOWN:{if(self->items_.empty())return 0;if(w==VK_RETURN&&self->selection_>=0){self->notify(Copy,self->selection_);return 0;}
            if(w==VK_HOME)self->scrollTo(0);else if(w==VK_END)self->scrollTo(self->layout_.height());else if(w==VK_PRIOR||w==VK_NEXT)self->scrollTo(self->offset_+(w==VK_PRIOR?-1:1)*self->logicalHeight());
            else if(w==VK_UP||w==VK_DOWN||w==VK_LEFT||w==VK_RIGHT){const int step=(w==VK_UP||w==VK_DOWN)?self->layout_.columns():1;self->selection_=self->selection_<0?0:std::clamp(self->selection_+(w==VK_UP||w==VK_LEFT?-step:step),0,int(self->items_.size())-1);if(self->selection_>=0){const int y=self->layout_.topOf(self->selection_),height=self->list_?ColorFlow::LIST_HEIGHT:ColorFlow::SWATCH_HEIGHT;if(y<self->offset_)self->scrollTo(y);else if(y+height>self->offset_+self->logicalHeight())self->scrollTo(y+height-self->logicalHeight());InvalidateRect(window,nullptr,FALSE);}}return 0;}
        case WM_NCDESTROY:self->window_=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        }
        return DefWindowProcW(window,message,w,l);
    }
public:
    ~ColorFlowView(){if(window_)DestroyWindow(window_);}
    void create(HWND owner,int id){id_=id;WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YeImageViewer.ColorFlow";cls.lpfnWndProc=procedure;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
        window_=CreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD|WS_TABSTOP,0,0,1,1,owner,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,this);}
    void place(int x,int y,int width,int height,int dpi,HFONT font,bool show,bool dark){dpi_=dpi;font_=font;dark_=dark;MoveWindow(window_,s(x),s(y),s(width),s(height),TRUE);ShowWindow(window_,show?SW_SHOW:SW_HIDE);relayout();}
    void set(std::vector<ColorFlow::Item> items,bool list=false,bool more=false,bool preserve=false){const int old=int(offset_);items_=std::move(items);list_=list;more_=more;selection_=pressed_=-1;hoverStar_=hoverBar_=false;layout_.rebuild(items_,logicalWidth(),list_);scrollTo(preserve?old:0);}
    void show(bool show){ShowWindow(window_,show?SW_SHOW:SW_HIDE);}
    void wheel(short delta){scrollTo(offset_-delta/120.*160);}
    void restoreScroll(int offset){scrollTo(offset);}
    void theme(bool dark){dark_=dark;InvalidateRect(window_,nullptr,FALSE);}
    void jump(const std::wstring& key){for(size_t i=0;i<items_.size();++i)if(items_[i].key==key){selection_=int(i);scrollTo(layout_.topOf(int(i)));break;}}
    const ColorFlow::Item* item(int index)const{return index>=0&&index<int(items_.size())?&items_[index]:nullptr;}
    HWND window()const{return window_;}
    int scroll()const{return int(offset_);}
};
