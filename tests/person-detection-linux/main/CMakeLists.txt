idf_component_register(
    SRCS "main.cpp"
         "person_detect_model_data.cc"
    EMBED_FILES "web/index.html" "web/style.css"
    INCLUDE_DIRS "."
)
