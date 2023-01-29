# Firmware for the [AirGradient DIY Air Quality Sensor](https://www.airgradient.com/open-airgradient/instructions/diy)

* 3-line display of temperature, humidity and CO2
* can provide metrics via local http server
* can push metrics to [Grafana Cloud](https://grafana.com/) (via [push using the Influx Line protocol](https://grafana.com/docs/grafana-cloud/data-configuration/metrics/metrics-influxdb/push-from-telegraf))

## Development/Usage
This project is developed with the excellent [PlatformIO](https://platformio.org) toolbox.
Rename `./platformio.ini.changeme` to `./platformio.ini` and set up values according to your liking.
Then open and build with PlatformIO.

## Inspiration
* Heavily inspired by Jeff Geerlings [great work](https://www.jeffgeerling.com/blog/2021/airgradient-diy-air-quality-monitor-co2-pm25) on the topic
* With ideas from [the original sample code](https://github.com/airgradienthq/arduino/blob/253d8a68103f567a0c16d9aa19835746f2843c73/examples/DIY_BASIC/DIY_BASIC.ino)
