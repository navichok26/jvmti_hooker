package ru.x5113nc3x.jvmti_test;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Build;
import android.os.Bundle;
import android.os.Debug;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.Button;
import android.view.View;

import java.io.IOException;
import java.lang.reflect.Method;


import ru.x5113nc3x.jvmti_test.databinding.ActivityMainBinding;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "ReflectionDemo";

    private ActivityMainBinding binding;
    private final String LIB_NAME = "libjvmti_test.so";


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        System.loadLibrary("jvmti_test");

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        TextView tv = binding.sampleText;
        tv.setText(stringFromJava());

        Button buttonSingle = binding.buttonSingle;
        buttonSingle.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Toast.makeText(MainActivity.this, stringFromJava(), Toast.LENGTH_SHORT).show();
            }
        });
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