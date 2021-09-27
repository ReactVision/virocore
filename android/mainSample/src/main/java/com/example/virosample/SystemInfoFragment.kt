package com.example.virosample

import android.os.Build
import android.os.Bundle
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat

class SystemInfoFragment : PreferenceFragmentCompat() {

    companion object {
        private const val PREFERENCE_ = "preference_"
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        // Load the preferences from an XML resource
        addPreferencesFromResource(R.xml.prefs)
        // TODO after next release you can use info from library
        //findPreference<Preference>(PREFERENCE_ + "APPVERSION")?.summary = com.viro.renderer.BuildConfig.VIRO_VERSION
        findPreference<Preference>(PREFERENCE_ + "APPVERSION")?.summary = BuildConfig.VERSION_NAME
        findPreference<Preference>(PREFERENCE_ + "APPVERSION")?.title =
            "${findPreference<Preference>(PREFERENCE_ + "APPVERSION")?.title} [${BuildConfig.FLAVOR}]"
        findPreference<Preference>(PREFERENCE_ + "BOARD")?.summary = Build.BOARD
        findPreference<Preference>(PREFERENCE_ + "BRAND")?.summary = Build.BRAND
        findPreference<Preference>(PREFERENCE_ + "CPU_ABI")?.summary = Build.SUPPORTED_ABIS[0]
        findPreference<Preference>(PREFERENCE_ + "DISPLAY")?.summary = Build.DISPLAY
        findPreference<Preference>(PREFERENCE_ + "USER")?.summary = Build.USER
    }
}