package com.example.virosample

import android.os.Bundle

class MLMainActivity : NavigationActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_ml_main)
        supportFragmentManager
            .beginTransaction()
            .add(R.id.contentInfo, SystemInfoFragment())
            .commit()

    }
}