#include "Host/Runtime.hpp"

#if defined(_WIN32)
#undef INPUT
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#endif

extern "C" void app_main();

namespace
{
#if defined(_WIN32)

    struct TimerResolution
    {
        TimerResolution() noexcept { timeBeginPeriod(1U); }
        ~TimerResolution() noexcept { timeEndPeriod(1U); }
    } g_timerResolution;
#endif

    int runSimulator()
    {
        app_main();

        auto &runtime = pipcore::desktop::Runtime::instance();

        if (runtime.width() == 0)
            runtime.openSetupWindow();

        while (!runtime.shouldQuit())
        {
            runtime.pumpEvents();
            runtime.delayMs(1);
        }

        return 0;
    }
}

#if defined(_WIN32) && defined(PIPSIM_GUI)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return runSimulator();
}
#else
int main()
{
    return runSimulator();
}
#endif
