package site.multitap.piccolo;

import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;

import java.io.File;

import org.libsdl.app.SDLActivity;

// piccolo's activity: SDL's activity with SDL3 linked statically into
// libmain.so, so only "main" is loaded. The display mode selection for
// Android TV (resolution + refresh rate) will live here too.
//
// Storage access was settled by StartActivity before this one started. With
// access, piccolo's folder is /sdcard/piccolo (it survives uninstalling): this
// class tells the native side through the environment variable
// MULTITAP_DATA_DIR (read by frontends-common app_paths), set before SDL_main
// runs. Without access it's the app's Android/media folder.
public class PiccoloActivity extends SDLActivity {
    private static final String TAG = "piccolo";

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    // A piccolo:// link that opened the app (StartActivity passes it on):
    // the native side finds it among the arguments (app_links_from_args).
    @Override
    protected String[] getArguments() {
        String link = link(getIntent());
        return link != null ? new String[] { link } : new String[0];
    }

    // Already running (singleInstance): the link comes as a dropped file
    // (SDL_EVENT_DROP_FILE; main.cpp hands it to app_links).
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        String link = link(intent);
        if (link != null) {
            SDLActivity.onNativeDropFile(link);
        }
    }

    // The intent's piccolo:// link, or null.
    private static String link(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction())) {
            return null;
        }
        Uri data = intent.getData();
        if (data == null || !"piccolo".equalsIgnoreCase(data.getScheme())) {
            return null;
        }
        return data.toString();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);   // loads libmain.so: nativeSetenv works from here on
        if (mBrokenLibraries) {
            return;
        }
        // Where browsing for games starts (app_paths_home_folder): SDL has no
        // user folders on Android.
        String sdcard = Environment.getExternalStorageDirectory().getAbsolutePath();
        nativeSetenv("MULTITAP_HOME_DIR", sdcard);
        // Each package keeps its own folder (their cores are in their own
        // private storage, and they install side by side; see build-apk.sh):
        // site.multitap.piccolo -> piccolo, .debug -> piccolo-debug,
        // .legacy -> piccolo-legacy, .legacy.debug -> piccolo-legacy-debug.
        String folder = getPackageName().replaceFirst("^site\\.multitap\\.", "").replace('.', '-');
        nativeSetenv("PICCOLO_FOLDER_NAME", folder);
        // When this APK was installed: the files it ships are copied out of
        // it again only when this changes (frontends-common's app_assets).
        try {
            long installed = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
            nativeSetenv("MULTITAP_APK_STAMP", Long.toString(installed));
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "can't read the package's update time: bundled files copied at every start");
        }
        if (StartActivity.hasSharedStorageAccess(this)) {
            nativeSetenv("MULTITAP_DATA_DIR", sdcard);
            Log.i(TAG, "data folder: " + sdcard + "/" + folder);
        } else {
            // Android/media/<package>: no permission needed, and unlike
            // Android/data, file managers can still open it (Android 11+).
            // Deleted on uninstall, so the storage dialog stays the better
            // choice. getExternalMediaDirs is deprecated since API 30 but works.
            File[] media = getExternalMediaDirs();
            if (media.length > 0 && media[0] != null) {
                String path = media[0].getAbsolutePath();
                nativeSetenv("MULTITAP_DATA_DIR", path);
                Log.w(TAG, "no storage access: data folder " + path + "/" + folder + " (deleted on uninstall)");
            } else {
                Log.w(TAG, "no storage access: using the app's private folder (deleted on uninstall)");
            }
        }
    }
}
