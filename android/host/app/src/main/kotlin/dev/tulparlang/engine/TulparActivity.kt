package dev.tulparlang.engine

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.os.PerformanceHintManager
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * Tulpar Engine Kotlin host (plan Faz 1, REV 5/11). Saf NativeActivity yerine:
 * Play Asset Delivery, Firebase, ADPF Performance Hint ve uygulama ici
 * guncelleme buradan baglanir. Motor `libtulpargame.so` icinde (jniLibs).
 *
 * DERLENMEDI (2026-09-14): bu makinede Android SDK/Gradle yok; ilk derlemede
 * imzalar jni_bridge.cpp ile birlikte dogrulanir.
 */
class TulparActivity : Activity(), SurfaceHolder.Callback {
    private external fun nativeSurfaceCreated(surface: Any)
    private external fun nativeSurfaceDestroyed()
    private external fun nativeSetForeground(fg: Boolean)
    private external fun nativeTouch(action: Int, pointer: Int, x: Float, y: Float, eventTimeMs: Long)

    private var hintSession: PerformanceHintManager.Session? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        System.loadLibrary("tulpargame")
        val view = SurfaceView(this)
        view.holder.addCallback(this)
        setContentView(view)
        // ADPF Performance Hint (EK A.4): elle core pinleme YOK, OS'a hedef kare
        // suresi bildirilir; oturum motorun render thread'i icin acilir.
        if (Build.VERSION.SDK_INT >= 31) {
            val mgr = getSystemService(PerformanceHintManager::class.java)
            hintSession = mgr?.createHintSession(intArrayOf(android.os.Process.myTid()), 16_666_666L)
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) = nativeSurfaceCreated(holder.surface)
    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}
    override fun surfaceDestroyed(holder: SurfaceHolder) = nativeSurfaceDestroyed()
    override fun onResume() { super.onResume(); nativeSetForeground(true) }
    override fun onPause() { nativeSetForeground(false); super.onPause() }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        // Zaman damgasi olay zamani (uptime ms), kare baslangici DEGIL (plan Faz 1).
        val idx = e.actionIndex
        nativeTouch(e.actionMasked, e.getPointerId(idx), e.getX(idx), e.getY(idx), e.eventTime)
        return true
    }
}
