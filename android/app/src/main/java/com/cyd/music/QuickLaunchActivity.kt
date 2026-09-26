package com.cyd.music

import android.app.Activity
import android.content.Intent
import android.os.Bundle

/** Shortcuts share the main page's app-selection flow. */
class QuickLaunchActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        startActivity(Intent(this, MainActivity::class.java))
        finish()
    }
}
