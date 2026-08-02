#ifndef LITEHTML_EXPORT_H
#define LITEHTML_EXPORT_H

// LITEHTML_API 导出/导入宏
//
// 用于将 litehtml 以动态库（DLL/Shared Library）形式构建并导出。
//  - 编译 litehtml DLL 时定义 LITEHTML_BUILD_DLL，LITEHTML_API 展开为 dllexport
//  - 消费者（插件等）include 头文件时未定义 LITEHTML_BUILD_DLL，展开为 dllimport
//  - 静态库/非 Windows 平台：空宏

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(LITEHTML_BUILD_DLL)
        #define LITEHTML_API __declspec(dllexport)
    #elif defined(LITEHTML_USE_DLL)
        #define LITEHTML_API __declspec(dllimport)
    #else
        #define LITEHTML_API
    #endif
#else
    #if defined(LITEHTML_BUILD_DLL) && defined(__GNUC__) && __GNUC__ >= 4
        #define LITEHTML_API __attribute__((visibility("default")))
    #else
        #define LITEHTML_API
    #endif
#endif

#endif // LITEHTML_EXPORT_H
