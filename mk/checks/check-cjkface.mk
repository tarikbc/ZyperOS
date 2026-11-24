CHECKS += check-cjkface
#| a Chinese or Japanese name is drawn in a face that has its characters whatever face it was asked
#| for: home.c's page faces (the top bar's song, the cassette's label, the grid's names) and
#| ui_face_at's measure each line as the Chinese or Japanese cut of their size and weight does, in
#| English, after the faces are opened again, and in 日本語 (skips without SDL2)
