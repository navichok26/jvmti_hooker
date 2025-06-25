package ru.x5113nc3x.jvmti_test;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.util.Log;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.Button;
import android.view.View;
import android.content.Intent;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import ru.x5113nc3x.jvmti_test.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "ReflectionDemo";

    // Used to load the 'jvmti_test' library on application startup.
    private ActivityMainBinding binding;
    private final String LIB_NAME = "libjvmti_test.so";


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
//        System.loadLibrary("jvmti_test");

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // Example of a call to a native method
        TextView tv = binding.sampleText;
        tv.setText(stringFromJava());

        // Добавляем обработчик нажатия на кнопку button_single
        Button buttonSingle = binding.buttonSingle;
        buttonSingle.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Toast.makeText(MainActivity.this, stringFromJava(), Toast.LENGTH_SHORT).show();
            }
        });

        try {
            // 1. Загружаем класс по имени
            Class<?> injectClass = Class.forName("ru.x5113nc3x.inject.InjectClass");

            // 2. Получаем метод getHelloWorld (без параметров)
            java.lang.reflect.Method method = injectClass.getDeclaredMethod("getHelloWorld");

            // 3. Вызываем статический метод (передаем null как экземпляр)
            String result = (String) method.invoke(null);

            // 4. Выводим результат в лог
            Log.i(TAG, "Result from injected class: " + result);

        } catch (ClassNotFoundException e) {
            Log.e(TAG, "Class not found: " + e.getMessage());
        } catch (NoSuchMethodException e) {
            Log.e(TAG, "Method not found: " + e.getMessage());
        } catch (IllegalAccessException e) {
            Log.e(TAG, "Access denied: " + e.getMessage());
        } catch (java.lang.reflect.InvocationTargetException e) {
            Log.e(TAG, "Invocation failed: " + e.getCause().getMessage());
        }
    }

    private File extractLibraryFromApk(String apkPath, String libName) throws IOException {
        File libDir = new File(getFilesDir(), "lib");
        if (!libDir.exists()) {
            libDir.mkdirs();
        }

        File extractedLib = new File(libDir, libName);
        try (ZipFile zipFile = new ZipFile(apkPath)) {
            ZipEntry entry = zipFile.getEntry("lib/arm64-v8a/" + libName);
            if (entry == null) {
                throw new IOException("Library not found in APK: " + libName);
            }

            try (InputStream is = zipFile.getInputStream(entry);
                 FileOutputStream fos = new FileOutputStream(extractedLib)) {
                byte[] buffer = new byte[1024];
                int length;
                while ((length = is.read(buffer)) > 0) {
                    fos.write(buffer, 0, length);
                }
            }
        }

        return extractedLib;
    }

    private void attachAgent(String agentPath, ClassLoader classLoader) throws IOException {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            Debug.attachJvmtiAgent(agentPath, null, classLoader);
        } else {
            try {
                Class<?> vmDebugClazz = Class.forName("dalvik.system.VMDebug");
                Method attachAgentMethod = vmDebugClazz.getMethod("attachAgent", String.class);
                attachAgentMethod.setAccessible(true);
                attachAgentMethod.invoke(null, agentPath);
            } catch (Exception ex) {
                ex.printStackTrace();
            }
        }
    }

    public native String stringFromJNI();

    public String stringFromJava() {
        return "call string from java";
    }
}