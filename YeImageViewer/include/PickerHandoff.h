#pragma once
#include <cstdint>

namespace PickerHandoff {
// 只有成功确认取色才由调用方创建。普通取消、C复制不结束均不进入窗口期。
class Gesture {
    uint64_t until_,releaseUntil_=0;
    bool held_=false,finished_=false,chosen_=false;
public:
    explicit Gesture(uint64_t now):until_(now+2000){}
    bool down(uint64_t now){if(finished_||now>=until_||held_)return false;held_=true;releaseUntil_=now+5000;return true;}
    bool up(uint64_t now){if(!held_)return false;held_=false;finished_=true;chosen_=now<releaseUntil_;return true;}
    void cancel(){if(!held_)finished_=true;}
    bool active(uint64_t now)const{return !finished_&&now<(held_?releaseUntil_:until_);}
    bool chosen()const{return chosen_;}
};
}
