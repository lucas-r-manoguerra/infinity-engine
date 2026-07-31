// tests/core/main.cpp
//
// The single TU that instantiates the doctest runner for this module's test
// executable. DOCTEST_CONFIG_NO_EXCEPTIONS is injected for every test TU via
// target_compile_definitions (infinity_add_test), so only the
// IMPLEMENT_WITH_MAIN definition lives here.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>
