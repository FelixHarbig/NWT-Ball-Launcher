set(srcs "main.cpp"
        "app_camera.cpp"
        "person_detection.cpp"
        "person_detect_model_data.cc"
        "sd_utils.cpp")

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS
        "."
)
