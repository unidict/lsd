//
//  test_platform.h
//  libud
//
//  Created by kejinlu on 2026/04/11.
//

#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H

#ifdef _WIN32
    #include <io.h>
    #define unlink _unlink
#else
    #include <unistd.h>
#endif

#endif /* TEST_PLATFORM_H */
