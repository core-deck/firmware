# Custom matrix implementation for PCF8574 I2C GPIO expander
CUSTOM_MATRIX = lite

# Required for custom matrix
SRC += matrix.c
I2C_DRIVER_REQUIRED = yes

# Register a press/release on the first scan that sees it, then ignore the
# key for DEBOUNCE ms. The default (sym_defer_g) waits for a second stable
# scan, so a quick tap or the gap between two taps is dropped when a
# display flush stalls scanning in between.
DEBOUNCE_TYPE = sym_eager_pk

# TFT Display - driver selected based on config.h
QUANTUM_PAINTER_ENABLE = yes

# Automatically include the correct driver based on config.h define
# To switch displays, edit config.h and uncomment the desired USE_* define
QUANTUM_PAINTER_DRIVERS += st7789_spi surface

# Raw HID for receiving display data from companion app
RAW_ENABLE = yes

# Display and HID protocol
SRC += display.c
SRC += protocol.c
SRC += graphics/terminus_bold_18.qff.c
SRC += graphics/terminus_reg_14.qff.c
SRC += graphics/logo.qgf.c

# Soft key runtime configuration
SRC += softkeys.c

# Tunable display color palette
SRC += theme.c

# Detect host OS for platform-aware modifier labels
OS_DETECTION_ENABLE = yes

# Custom RGB matrix effect (breathing + reactive)
RGB_MATRIX_CUSTOM_KB = yes
