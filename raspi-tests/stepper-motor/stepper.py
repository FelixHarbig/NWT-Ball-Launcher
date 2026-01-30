"""
This Raspberry Pi code was developed by newbiely.com
This Raspberry Pi code is made available for public use without any restriction
For comprehensive instructions and wiring diagrams, please visit:
https://newbiely.com/tutorials/raspberry-pi/raspberry-pi-28byj-48-stepper-motor-uln2003-driver

Funny thing is that this script needed to be modified as the source did not work.
Also, do not install python3-rpi.gpio on modern Raspberry Pi OS versions as it
breaks the existing RPi.GPIO package. Just use the preinstalled package.

A single stepper works without an external power source.
"""


import RPi.GPIO as GPIO
import time

# Define GPIO pins for ULN2003 driver
IN1 = 23
IN2 = 24
IN3 = 25
IN4 = 8

# Set GPIO mode and configure pins
GPIO.setmode(GPIO.BCM)
GPIO.setup(IN1, GPIO.OUT)
GPIO.setup(IN2, GPIO.OUT)
GPIO.setup(IN3, GPIO.OUT)
GPIO.setup(IN4, GPIO.OUT)

# Define constants
STEPS_PER_REVOLUTION = 512 # 4096

# Define sequence for 28BYJ-48 stepper motor
seq = [
    [1, 0, 0, 1],
    [1, 0, 0, 0],
    [1, 1, 0, 0],
    [0, 1, 0, 0],
    [0, 1, 1, 0],
    [0, 0, 1, 0],
    [0, 0, 1, 1],
    [0, 0, 0, 1]
]

# Function to rotate the stepper motor one step
def step(delay, step_sequence):
    GPIO.output(IN1, step_sequence[0])
    GPIO.output(IN2, step_sequence[1])
    GPIO.output(IN3, step_sequence[2])
    GPIO.output(IN4, step_sequence[3])
    time.sleep(delay)

# Function to move the stepper motor one step forward
def step_forward(delay, steps):
    for _ in range(steps):
        for s in seq:
            step(delay, s)

# Function to move the stepper motor one step backward
def step_backward(delay, steps):
    for _ in range(steps):
        for s in reversed(seq):
            step(delay, s)

try:
    # Set the delay between steps
    delay = 0.0015

    while True:
        # Rotate one revolution forward (clockwise)
        step_forward(delay, STEPS_PER_REVOLUTION)

        # Pause for 2 seconds
        time.sleep(2)

        # Rotate one revolution backward (anticlockwise)
        step_backward(delay, STEPS_PER_REVOLUTION)

        # Pause for 2 seconds
        time.sleep(2)

except KeyboardInterrupt:
    print("\nExiting the script.")

finally:
    # Clean up GPIO settings
    GPIO.cleanup()
