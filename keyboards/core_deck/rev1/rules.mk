# Custom matrix implementation for PCF8574 I2C GPIO expander
CUSTOM_MATRIX = lite

# Required for custom matrix
SRC += matrix.c
I2C_DRIVER_REQUIRED = yes

# YOLO mode toggle switch
DIP_SWITCH_ENABLE = yes

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
