package ru.x5113nc3x.jvmti_test;

public class SecretClass {
    public String getFlag_public() {
        return "flag{public}";
    }

    private String getFlag_private() {
        return "flag{private}";
    }
}
