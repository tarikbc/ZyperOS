CHECKS += check-shaderpic
#| the Shader and Strength lists' pictures as the launcher loads them: every filter and Strength
#| step on each panel a texture with an alpha channel, opaque, since the Pixel 2's SDL (2.32,
#| GLES2) draws an RGB888 texture into the screen's target with alpha 0 and the panel's copy then
#| shows it black (a computer's SDL writes 255, so no shot sees it); a picture not on the card
#| NULL, said once and not read each frame; on the launcher's own objects
