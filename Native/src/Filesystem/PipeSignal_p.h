#pragma once
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#include <signal.h>
namespace iiFileProvider::native::detail {
// Keep an EPIPE on a borrowed pipe from terminating the application. Restore
// this thread's mask and preserve a SIGPIPE that was already pending on entry.
class PipeSignal final {
    sigset_t pipe_{}, previous_{};
    bool active_=false, pending_=false, broken_=false;
public:
    explicit PipeSignal(bool sequential) {
        if(!sequential)return;
        sigemptyset(&pipe_);sigaddset(&pipe_,SIGPIPE);
        active_=pthread_sigmask(SIG_BLOCK,&pipe_,&previous_)==0;
        if(active_){sigset_t pending{};sigpending(&pending);pending_=sigismember(&pending,SIGPIPE)==1;}
    }
    void broken()noexcept{broken_=true;}
    ~PipeSignal(){
        if(!active_)return;
        if(broken_&&!pending_){sigset_t pending{};sigpending(&pending);if(sigismember(&pending,SIGPIPE)==1){int signal;sigwait(&pipe_,&signal);}}
        pthread_sigmask(SIG_SETMASK,&previous_,nullptr);
    }
};
}
#else
namespace iiFileProvider::native::detail {struct PipeSignal {explicit PipeSignal(bool){}void broken()noexcept{}};}
#endif
