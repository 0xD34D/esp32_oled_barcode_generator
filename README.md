# ESP32 UPC-A Generator

This is a simple project that demonstrates generating 12 digit UPC-A bar codes and displays them on a 128x32 OLED display


## How to use
First, make sure you configure your device via `idf.py menuconfig` or however you so choose to do so.

Then configure the pins that the display is hooked up to in `main.c`
```
// display I2C pins
#define I2C_SDA GPIO_NUM_1
#define I2C_SCL GPIO_NUM_2
```

## Displaying barcodes from the console
To display a barcode on the display, simply use the `barcode` command from the console
### Display EAN-8
```
esp32s3> barcode 12344321
```
![](files/ean8_12344321.png)
### Display EAN-13
```
esp32s3> barcode 5901234123457
```
![](files/ean13_5901234123457.png)
### Display UPC-A
```
esp32s3> barcode 012345678912
```
![](files/upca_012345678912.png)
