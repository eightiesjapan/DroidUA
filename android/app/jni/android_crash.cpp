#include <SDL.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <ucontext.h>
#include <cstdio>
#include <initializer_list>
#include <mutex>
#include <string>
#include <ctime>

namespace {
char reportPath[1024];
std::string logPath;
std::mutex logMutex;
SDL_LogOutputFunction androidLog=nullptr;
void* androidLogData=nullptr;
void log(void*,int category,SDL_LogPriority priority,const char* message) {
    if(androidLog) androidLog(androidLogData,category,priority,message);
    std::lock_guard<std::mutex> guard(logMutex);
    // Close on each message: even a fatal crash leaves earlier log entries on disk.
    FILE* file=std::fopen(logPath.c_str(),"a");
    if(!file) return;
    std::fseek(file,0,SEEK_END);
    if(std::ftell(file)>=256*1024) {
        std::fclose(file);
        for(int i=3;i>=1;--i) {
            auto from=i==1?logPath:logPath+"."+std::to_string(i-1);
            auto to=logPath+"."+std::to_string(i);std::rename(from.c_str(),to.c_str());
        }
        file=std::fopen(logPath.c_str(),"w");if(!file) return;
    }
    std::fprintf(file,"%lld [%d:%d] %.16000s\n",static_cast<long long>(std::time(nullptr)),category,int(priority),message);
    std::fclose(file);
}
struct sigaction previous[NSIG];
void literal(int fd,const char* s) { const char* p=s;while(*p) ++p; (void)!write(fd,s,p-s); }
void number(int fd,unsigned long long value) {
    char digits[32];unsigned n=0;do { digits[n++]="0123456789abcdef"[value&15];value>>=4; } while(value);
    char out[32];for(unsigned i=0;i<n;++i) out[i]=digits[n-i-1]; (void)!write(fd,out,n);
}
void crash(int signal,siginfo_t* info,void* context) {
    // Only async-signal-safe syscalls and fixed stack buffers here. Android's
    // existing handler still receives the fault and produces its normal tombstone.
    int fd=open(reportPath,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(fd>=0) {
        literal(fd,"DroidUA native fatal signal (numbers hexadecimal)\nsignal: 0x");number(fd,signal);
        literal(fd,"\npid: 0x");number(fd,getpid());
        literal(fd,"\nfault address: 0x");number(fd,info && (signal==SIGSEGV || signal==SIGBUS)?reinterpret_cast<unsigned long long>(info->si_addr):0);
#if defined(__aarch64__)
        auto* state=static_cast<ucontext_t*>(context);
        literal(fd,"\npc: 0x");number(fd,state->uc_mcontext.pc);
        literal(fd,"\nlr: 0x");number(fd,state->uc_mcontext.regs[30]);
#endif
        literal(fd,"\nReopen DroidUA and export diagnostics for logs and Android's crash trace.\n");close(fd);
    }
    const auto& old=previous[signal];
    if(old.sa_handler!=SIG_DFL && old.sa_handler!=SIG_IGN && old.sa_handler) {
        if(old.sa_flags&SA_SIGINFO) old.sa_sigaction(signal,info,context);
        else old.sa_handler(signal);
    }
    struct sigaction fallback={};fallback.sa_handler=SIG_DFL;sigemptyset(&fallback.sa_mask);sigaction(signal,&fallback,nullptr);
    kill(getpid(),signal);
}
}
void UAAndroidInstallCrashHandler() {
    const char* path=SDL_AndroidGetInternalStoragePath();if(!path) return;
    std::snprintf(reportPath,sizeof(reportPath),"%s/diagnostics/native-crash.txt",path);
    logPath=std::string(path)+"/diagnostics/game-log.txt";
    SDL_LogGetOutputFunction(&androidLog,&androidLogData);SDL_LogSetOutputFunction(log,nullptr);
    SDL_Log("DroidUA native diagnostics started");
    for(int signal:{SIGSEGV,SIGABRT,SIGBUS,SIGILL,SIGFPE}) {
        sigaction(signal,nullptr,&previous[signal]);
        struct sigaction action={};action.sa_sigaction=crash;sigemptyset(&action.sa_mask);
        action.sa_flags=SA_SIGINFO|SA_RESETHAND|(previous[signal].sa_flags&SA_ONSTACK);
        sigaction(signal,&action,nullptr);
    }
}
