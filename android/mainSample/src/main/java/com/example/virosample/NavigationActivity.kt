package com.example.virosample

import android.content.Intent
import android.os.Bundle
import android.view.Menu
import android.view.MenuItem
import android.widget.TextView
import androidx.appcompat.app.ActionBarDrawerToggle
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.Toolbar
import androidx.core.view.GravityCompat
import androidx.drawerlayout.widget.DrawerLayout
import androidx.multidex.BuildConfig
import com.google.android.material.navigation.NavigationView

abstract class NavigationActivity : AppCompatActivity(), NavigationView.OnNavigationItemSelectedListener {
    override fun onPostCreate(savedInstanceState: Bundle?) {
        super.onPostCreate(savedInstanceState)
        val toolbar = findViewById<Toolbar>(R.id.toolbar)
        setSupportActionBar(toolbar)
        val drawer = findViewById<DrawerLayout>(R.id.drawer_layout)
        val toggle = ActionBarDrawerToggle(
            this, drawer, toolbar, R.string.navigation_drawer_open, R.string.navigation_drawer_close
        )
        drawer.addDrawerListener(toggle)
        toggle.syncState()
        val navigationView = findViewById<NavigationView>(R.id.nav_view)
        navigationView.setNavigationItemSelectedListener(this)

        val headerLayout = navigationView.getHeaderView(0)
        val textVersion = headerLayout.findViewById<TextView>(R.id.textVersion)
        textVersion.text = BuildConfig.VERSION_NAME
    }

    override fun onBackPressed() {
        val drawer = findViewById<DrawerLayout>(R.id.drawer_layout)
        if (drawer.isDrawerOpen(GravityCompat.START)) {
            drawer.closeDrawer(GravityCompat.START)
        } else {
            super.onBackPressed()
        }
    }

    override fun onNavigationItemSelected(item: MenuItem): Boolean {
        // Handle navigation view item clicks here.
        val id = item.itemId
        if (id == R.id.nav_black_panther) {
            openActivity(ViroActivityAR::class.java)
        } else if (id == R.id.nav_hello_world) {
            openActivity(com.example.virosample.hello.ViroActivity::class.java)
        } else if (id == R.id.nav_placing_objects) {
            openActivity(com.example.virosample.placingObjects.ViroActivity::class.java)
        } else if (id == R.id.nav_retail) {
            openActivity(com.example.virosample.retail.ProductSelectionActivity::class.java)
        } else if (id == R.id.nav_tesla) {
            openActivity(com.example.virosample.tesla.ViroActivityAR::class.java)
        }
        val drawer = findViewById<DrawerLayout>(R.id.drawer_layout)
        drawer.closeDrawer(GravityCompat.START)
        return true
    }

    private fun openActivity(clazz: Class<*>) {
        startActivity(Intent(this, clazz))
        if (clazz.isInstance(NavigationActivity::class.java)) {
            finish()
        }
    }

    override fun onResume() {
        super.onResume()
        invalidateOptionsMenu()
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main_menu, menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.action_update -> {
                true
            }
            R.id.action_logcat -> {
                true
            }
            else -> super.onOptionsItemSelected(item)
        }
    }
}