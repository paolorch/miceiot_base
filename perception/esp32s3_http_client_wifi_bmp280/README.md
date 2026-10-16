## Implementación

Este proyecto parte del ejemplo original del docente para el **ESP32-S3 con sensor BMP280**, utilizando ESP-IDF v5.5.5 y comunicación HTTP mediante Wi-Fi.

Se ha adaptado el firmware para permitir **dos modalidades de adquisición de datos**, conservando la implementación original del sensor BMP280:

- **Modo BMP280:** adquisición de presión y temperatura ambientales mediante comunicación I²C.
- **Modo potenciómetros:** adquisición de tres señales analógicas mediante ADC, utilizadas para simular presión, temperatura y humedad.

El objetivo de esta adaptación es permitir el desarrollo y las pruebas del sistema IoT incluso cuando no se dispone físicamente del BMP280, sin perder la posibilidad de utilizar el sensor original.

## Selección del modo de funcionamiento

La modalidad se selecciona en el archivo `main/esp32s3_http_client_wifi_bmp280.c`, mediante la siguiente definición:

```c
#define USE_POTENTIOMETERS 1
```

**Para utilizar los tres potenciómetros:**

```c
#define USE_POTENTIOMETERS 1
```

**Para utilizar el sensor BMP280 original:**

```c
#define USE_POTENTIOMETERS 0
```

Después de cambiar el valor, es necesario volver a compilar y grabar el firmware en el ESP32-S3.

## Conexiones de hardware

**Modo potenciómetros (probado físicamente):**

- GPIO4: presión simulada.
- GPIO5: temperatura simulada.
- GPIO6: humedad simulada.
- Terminales exteriores de cada potenciómetro: 3.3 V y GND.
- Terminal central: entrada ADC correspondiente.

**Modo BMP280 (implementación original conservada):**

- SDA: GPIO8.
- SCL: GPIO9.
- Alimentación y tierra según las especificaciones del módulo.

## Estado de las pruebas

La modalidad con tres potenciómetros fue compilada, grabada y probada físicamente sobre el ESP32-S3.

Se verificó la adquisición mediante ADC, la conexión Wi-Fi, el envío HTTP y el almacenamiento de temperatura y humedad simuladas en MongoDB.

**La modalidad BMP280 conserva el código original del profesor.**
