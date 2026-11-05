// SPDX-License-Identifier: GPL-2.0-or-later
// openMERC Android: SDL3's activity around libmain.so (the engine). The command line comes from args.txt in the app's
// external files folder (adb push), so a test run needs no new APK; without it the GLES 3 renderer starts the menu.
package com.openmerc.game;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;

import org.libsdl.app.SDLActivity;

public class OpenMercActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // the C++ runtime and OpenAL first: libmain.so, OMsnd.so and the game modules need them
        return new String[] {"c++_shared", "SDL3", "openal", "main"};
    }

    /* vid_surfacescale (engine, glimp_sdl3.c): the window surface gets a fixed size below the panel's and the display
       hardware scales it up for free - the usual way a phone game renders below the panel resolution (2400x1080
       here). w <= 0: the surface follows the layout again. */
    public static void setSurfaceSize(final int w, final int h) {
        final SDLActivity a = mSingleton;

        if (a == null) {
            return;
        }

        a.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (mSurface == null) {
                    return;
                }

                if (w <= 0 || h <= 0) {
                    mSurface.getHolder().setSizeFromLayout();
                } else {
                    mSurface.getHolder().setFixedSize(w, h);
                }
            }
        });
    }

    @Override
    protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<String>();

        args.add("+set");
        args.add("vid_renderer");
        args.add("gles3");

        try {
            File dir = getExternalFilesDir(null);
            File f = new File(dir, "args.txt");

            if (f.exists()) {
                BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
                String line;

                while ((line = r.readLine()) != null) {
                    for (String a : line.trim().split("\\s+")) {
                        if (a.length() > 0) {
                            args.add(a);
                        }
                    }
                }

                r.close();
            }
        } catch (Exception e) {
            // no arguments file: the defaults above
        }

        return args.toArray(new String[0]);
    }
}
