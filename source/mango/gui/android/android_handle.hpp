/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2016 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/gui/window.hpp>

namespace mango
{

    struct WindowHandle
    {
    	ANativeWindow* window;

        WindowHandle(int width, int height);
        ~WindowHandle();
    };

} // namespace mango
