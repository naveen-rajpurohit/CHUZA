#include "EnvSensorPrinter.h"

void printEnvReading(EnvSensor &env) {
    if (!env.isReady()) {
        Serial.println("[Env] sensor not ready yet");
        return;
    }

    Serial.print("[Env] T: ");
    Serial.print(env.getTemperatureC(), 2);
    Serial.print(" C  H: ");
    Serial.print(env.getHumidityPct(), 2);
    Serial.print(" %  P: ");
    Serial.print(env.getPressureHpa(), 2);
    Serial.print(" hPa  Alt: ");
    Serial.print(env.getAltitudeM(), 2);
    Serial.println(" m");
}