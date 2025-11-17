set(srcs "main.cpp"
        "app_camera.cpp"
        "motor_control.cpp"
        "person_detection.cpp"
        "web_server.cpp"
        "person_detect_model_data.cc"
        "sd_utils.cpp")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "."
    EMBED_FILES
        "web/index.html"
    # REQUIRES
    #     "esp_http_server"
    #    "esp_wifi"
    #    "esp-tflite-micro"
    #    "nvs_flash"
    #    "esp32-camera"

)
