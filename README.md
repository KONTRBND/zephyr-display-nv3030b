# NV3030B Display Driver for Zephyr

This repository contains a **minimally working Zephyr display driver** for the **Novatek NV3030B** SPI LCD controller.

The driver was developed as part of a **ZMK-based project**, but it is not ZMK-specific and should work in other Zephyr-based projects with similar hardware. That said, this is a **crude implementation**: it works for our setup, but it is not heavily abstracted, optimized, or battle-tested.

If you are looking for a polished, fully configurable upstream-quality driver, this is not it (yet), but it may work.

## Status and Caveats

* This is a **basic, working implementation**
* Tested with **one specific display module**
* Assumes several **fixed parameters** (pixel format, color order, inversion)
* Likely needs refinement for broader compatibility
* Works for us; may break for you

Treat this as a **starting point**, not a drop-in universal solution.

## Supported Hardware

The driver has been tested with:

* **Waveshare 1.83″ LCD Display Module**

  * Resolution: **240×280**
  * Interface: **SPI**
  * Panel type: IPS
  * Colors: RGB565 (65K)
  * Rounded corners

Other NV3030B-based panels should work, but are *untested*.

## SPI Requirements and Limitations

The following SPI configuration was required for the tested panel:

* `SPI_MODE_CPOL | SPI_MODE_CPHA`
* `SPI_HOLD_ON_CS`
* `spi-max-frequency = <2000000>;`

Anything above **2 MHz** caused display corruption or failure on the tested hardware.

Other panels may require different SPI modes. These settings are not guaranteed to be correct for all NV3030B displays.

## Driver Features and Limitations

### What works

* Basic initialization
* Full-frame writes
* RGB565 pixel format
* Zephyr `display` API integration
* Backlight control via GPIO

### Hardcoded / fixed behavior

* **Pixel format:** RGB565 only
* **Color order:** RGB (not BGR)
* **Display inversion:** always enabled
  (required to get correct colors instead of CMY on the tested panel)
* **Orientation:** normal only

### What is missing or incomplete

* Orientation support
* Power management / sleep handling
* Partial updates / damage tracking
* Runtime configuration
* Robust error handling
* Validation across multiple panels

## Building and Enabling the Driver

This repository is structured as an **out-of-tree Zephyr module**.

It provides:

* Kconfig integration
* CMake integration
* Devicetree bindings for `novatek,nv3030b`

Once the module is included, enable the driver via Kconfig:

```
CONFIG_DISPLAY=y
CONFIG_DISPLAY_NV3030B=y
```

The driver is enabled automatically when a matching devicetree node is present.

## Addings the Module via `west`
Since this driver is not part of upstream Zephyr, it must be added as an external module using `west`.

Add the following to your project’s `west.yml`:

```yaml
manifest:
  remotes:
    - name: zephyr-display-nv3030b
      url-base: https://github.com/KONTRBND
  projects:
    - name: zephyr-display-nv3030b
      remote: zephyr-display-nv3030b
      import: west.yml
```

After updating the manifest, run:

```sh
west update
```

Zephyr should then automatically pick up the module via `zephyr/module.yml`, including the CMake, Kconfig, and devicetree bindings.

## Devicetree Binding

Compatible string:

```
compatible = "novatek,nv3030b";
```

### Required properties

* `width` – display width in pixels
* `height` – display height in pixels
* `dc-gpios`
* `reset-gpios`
* `bl-gpios`
* SPI configuration (`reg`, `spi-max-frequency`, etc.)

### Optional properties

* `panel-offset` – vertical pixel offset (default: `0`)

## Example Devicetree (ZMK / Zephyr)

Below is a real-world example using the driver in a ZMK-based setup:

```dts
&spi2 {
    status = "okay";
    cs-gpios = <&xiao_d 7 GPIO_ACTIVE_LOW>;

    display: display@0 {
        compatible = "novatek,nv3030b";
        reg = <0>;

        spi-max-frequency = <2000000>;

        width = <240>;
        height = <280>;

        dc-gpios = <&xiao_d 9 GPIO_ACTIVE_HIGH>;
        reset-gpios = <&gpio0 10 GPIO_ACTIVE_LOW>;
        bl-gpios = <&gpio0 9 GPIO_ACTIVE_HIGH>;

        panel-offset = <0>;
    };
};
```

## Upstreaming and Future Work

This driver is currently intended to remain **out-of-tree**.

Upstreaming to Zephyr is not an immediate goal, but improving compatibility, configurability, and overall quality may make that possible in the future.

## Final Notes

This driver exists because it was needed, not because it is perfect.

If you are comfortable working with Zephyr internals, SPI displays, and devicetree, this should be usable and extensible. If not, expect friction.

Contributions, fixes, and improvements are welcome, but expect to do some real debugging.
