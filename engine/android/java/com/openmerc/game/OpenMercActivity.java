// SPDX-License-Identifier: GPL-2.0-or-later
// openMERC Android: SDL3's activity around libmain.so (the engine). The command line comes from args.txt in the app's
// external files folder (adb push), so a test run needs no new APK; without it the GLES 3 renderer starts the menu.
// openMERC's data archive comes with the APK (assets/zz_openmerc.pak) and is put next to the game data on start.
package com.openmerc.game;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.ArrayList;

import org.libsdl.app.SDLActivity;

public class OpenMercActivity extends SDLActivity {
    /* multiplayer: Android drops broadcast packets for apps without a multicast lock - a phone hosting a game would not
       answer the LAN search of a PC (or another phone) */
    private android.net.wifi.WifiManager.MulticastLock lanLock;

    @Override
    protected void onCreate(android.os.Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        try {
            android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);

            if (wm != null) {
                lanLock = wm.createMulticastLock("openmerc-lan");
                lanLock.setReferenceCounted(false);
                lanLock.acquire();
            }
        } catch (Exception e) {
            // no Wi-Fi service: internet games still work
        }
    }

    @Override
    protected void onDestroy() {
        if (lanLock != null && lanLock.isHeld()) {
            lanLock.release();
        }

        super.onDestroy();
    }

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

                /* the view keeps the whole screen (SDL adds it as wrap_content: a SurfaceView with a fixed buffer size
                   would measure itself to that size); only its buffer is smaller */
                android.view.ViewGroup.LayoutParams lp = mSurface.getLayoutParams();

                if (lp != null) {
                    lp.width = android.view.ViewGroup.LayoutParams.MATCH_PARENT;
                    lp.height = android.view.ViewGroup.LayoutParams.MATCH_PARENT;
                    mSurface.setLayoutParams(lp);
                }

                if (w <= 0 || h <= 0) {
                    mSurface.getHolder().setSizeFromLayout();
                } else {
                    mSurface.getHolder().setFixedSize(w, h);
                }

                mSurface.requestLayout();
            }
        });
    }

    /* openMERC's own data (zz_openmerc.pak: its configs, menu pages, fonts) ships in the APK as an asset; the engine
       reads it from <external files folder>/openmerc. It is copied there when the copy is missing or older than the
       installed APK. Without it the game's stock configs ask for an exclusive fullscreen mode that Android does not
       have, and no renderer starts. */
    private void installData() {
        try {
            File dir = new File(getExternalFilesDir(null), "openmerc");
            File out = new File(dir, "zz_openmerc.pak");
            long apk = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;

            if (out.isFile() && out.lastModified() >= apk) {
                return;
            }

            dir.mkdirs();

            File tmp = new File(dir, "zz_openmerc.pak.tmp");
            InputStream in = getAssets().open("zz_openmerc.pak");
            OutputStream o = new FileOutputStream(tmp);
            byte[] buf = new byte[65536];
            int n;

            try {
                while ((n = in.read(buf)) > 0) {
                    o.write(buf, 0, n);
                }
            } finally {
                o.close();
                in.close();
            }

            out.delete();

            if (!tmp.renameTo(out)) {
                tmp.delete();
            }
        } catch (Exception e) {
            // no asset (an APK built without it) or no storage: the engine reports what it misses
        }
    }

    @Override
    protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<String>();

        // runs on the SDL thread right before the engine's main
        installData();

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
