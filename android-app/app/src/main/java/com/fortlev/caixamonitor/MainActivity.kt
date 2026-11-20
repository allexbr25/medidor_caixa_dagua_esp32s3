package com.fortlev.caixamonitor

import android.annotation.SuppressLint
import android.graphics.Color
import android.os.Bundle
import android.text.InputType
import android.util.Base64
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import java.security.SecureRandom
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.PBEKeySpec

class MainActivity : AppCompatActivity() {

    private lateinit var securePrefs: android.content.SharedPreferences
    private lateinit var rootContainer: FrameLayout
    private lateinit var authLayout: LinearLayout
    private lateinit var webView: WebView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Inicializa o cofre criptografado no Android Keystore (AES-256-GCM)
        val masterKey = MasterKey.Builder(this)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()

        securePrefs = EncryptedSharedPreferences.create(
            this,
            "caixa_fortlev_secure_vault",
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM
        )

        rootContainer = FrameLayout(this).apply {
            setBackgroundColor(Color.parseColor("#020617"))
        }

        setupWebView()
        setupAuthScreen()

        setContentView(rootContainer)

        // Se já houver usuário com digital ativa, abre o leitor de digital do Android automaticamente
        if (hasRegisteredUser() && securePrefs.getBoolean("biometric_enabled", false)) {
            promptBiometricLogin()
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun setupWebView() {
        webView = WebView(this).apply {
            visibility = View.GONE
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.cacheMode = WebSettings.LOAD_NO_CACHE
            setBackgroundColor(Color.parseColor("#020617"))
            webViewClient = object : WebViewClient() {
                override fun onPageFinished(view: WebView?, url: String?) {
                    super.onPageFinished(view, url)
                    // Desbloqueia automaticamente o painel web pois a autenticação nativa do Android já validou o usuário
                    val user = securePrefs.getString("username", "admin") ?: "admin"
                    evaluateJavascript("if(typeof unlockDashboard==='function'){unlockDashboard('$user');}", null)
                }
            }
        }
        rootContainer.addView(
            webView,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    private fun setupAuthScreen() {
        authLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(64, 64, 64, 64)
        }

        val title = TextView(this).apply {
            text = "💧 Caixa d'Água 1.000 L"
            textSize = 22f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
        }

        val subtitle = TextView(this).apply {
            text = if (hasRegisteredUser())
                "Autentique-se com sua Impressão Digital ou Senha"
            else
                "Primeiro Acesso: Cadastre seu Usuário e Senha no Cofre Seguro (Android Keystore)"
            textSize = 13f
            setTextColor(Color.parseColor("#94A3B8"))
            gravity = Gravity.CENTER
            setPadding(0, 12, 0, 48)
        }

        val userInput = EditText(this).apply {
            hint = "Usuário"
            setHintTextColor(Color.parseColor("#64748B"))
            setTextColor(Color.WHITE)
            setText(securePrefs.getString("username", ""))
        }

        val passInput = EditText(this).apply {
            hint = "Senha (mín. 6 caracteres)"
            setHintTextColor(Color.parseColor("#64748B"))
            setTextColor(Color.WHITE)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }

        val btnPrimary = Button(this).apply {
            text = if (hasRegisteredUser()) "Entrar com Senha" else "Cadastrar e Ativar Digital"
            setBackgroundColor(Color.parseColor("#0284C7"))
            setTextColor(Color.WHITE)
            setOnClickListener {
                val u = userInput.text.toString().trim().lowercase()
                val p = passInput.text.toString()
                if (u.length < 3 || p.length < 6) {
                    Toast.makeText(this@MainActivity, "Informe usuário (>=3) e senha (>=6).", Toast.LENGTH_SHORT).show()
                    return@setOnClickListener
                }
                if (!hasRegisteredUser()) {
                    registerUser(u, p)
                } else {
                    loginWithPassword(u, p)
                }
            }
        }

        val btnBio = Button(this).apply {
            text = "👆 Entrar com Impressão Digital"
            setBackgroundColor(Color.parseColor("#059669"))
            setTextColor(Color.WHITE)
            visibility = if (hasRegisteredUser()) View.VISIBLE else View.GONE
            setOnClickListener { promptBiometricLogin() }
        }

        authLayout.addView(title)
        authLayout.addView(subtitle)
        authLayout.addView(userInput)
        authLayout.addView(passInput)
        authLayout.addView(btnPrimary)
        authLayout.addView(btnBio)

        rootContainer.addView(
            authLayout,
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    private fun hasRegisteredUser(): Boolean {
        return securePrefs.contains("password_hash")
    }

    private fun hashPasswordPBKDF2(password: String, salt: ByteArray): String {
        val spec = PBEKeySpec(password.toCharArray(), salt, 100000, 256)
        val factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
        val hash = factory.generateSecret(spec).encoded
        return Base64.encodeToString(hash, Base64.NO_WRAP)
    }

    private fun registerUser(username: String, pass: String) {
        val salt = ByteArray(16)
        SecureRandom().nextBytes(salt)
        val hash = hashPasswordPBKDF2(pass, salt)

        securePrefs.edit()
            .putString("username", username)
            .putString("password_salt", Base64.encodeToString(salt, Base64.NO_WRAP))
            .putString("password_hash", hash)
            .putBoolean("biometric_enabled", true)
            .apply()

        Toast.makeText(this, "Conta criptografada no Android Keystore!", Toast.LENGTH_SHORT).show()
        promptBiometricLogin()
    }

    private fun loginWithPassword(username: String, pass: String) {
        val savedUser = securePrefs.getString("username", "")
        val saltB64 = securePrefs.getString("password_salt", "") ?: ""
        val savedHash = securePrefs.getString("password_hash", "") ?: ""

        val salt = Base64.decode(saltB64, Base64.NO_WRAP)
        val candidateHash = hashPasswordPBKDF2(pass, salt)

        if (username == savedUser && candidateHash == savedHash) {
            openDashboard()
        } else {
            Toast.makeText(this, "Usuário ou senha inválidos.", Toast.LENGTH_SHORT).show()
        }
    }

    private fun promptBiometricLogin() {
        val biometricManager = BiometricManager.from(this)
        val canAuth = biometricManager.canAuthenticate(
            BiometricManager.Authenticators.BIOMETRIC_STRONG or BiometricManager.Authenticators.BIOMETRIC_WEAK
        )
        if (canAuth != BiometricManager.BIOMETRIC_SUCCESS) {
            openDashboard()
            return
        }

        val executor = ContextCompat.getMainExecutor(this)
        val prompt = BiometricPrompt(this, executor, object : BiometricPrompt.AuthenticationCallback() {
            override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                super.onAuthenticationSucceeded(result)
                openDashboard()
            }

            override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                super.onAuthenticationError(errorCode, errString)
                Toast.makeText(this@MainActivity, errString, Toast.LENGTH_SHORT).show()
            }
        })

        val promptInfo = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Desbloquear Monitor Caixa d'Água")
            .setSubtitle("Toque no sensor de impressão digital para acessar")
            .setNegativeButtonText("Usar Senha")
            .build()

        prompt.authenticate(promptInfo)
    }

    private fun openDashboard() {
        authLayout.visibility = View.GONE
        webView.visibility = View.VISIBLE
        webView.loadUrl("https://allexbr25.github.io/medidor_caixa_dagua_esp32s3/")
    }
}
