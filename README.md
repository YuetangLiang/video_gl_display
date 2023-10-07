# video_gl_display

  It plays video data using OpenGL.
  It support read file and show it on the screen.
  It's use a Texture Shaders.
 
# The process is shown as follows
 
## [Init]
```c
  glutInit(): Init glut library.
  glutInitDisplayMode(): Set display mode.
  glutCreateWindow(): Create a window.
  glewInit(): Init glew library.
  glutDisplayFunc(): Set the display callback.
  glutTimerFunc(): Set timer.
  InitShaders(): Set Shader, Init Texture. It contains some functions about Shader.
  glutMainLoop(): Start message loop.
```
## [Loop to Render data]
```c
  glActiveTexture(): Active a Texture unit 
  glBindTexture(): Bind Texture
  glTexImage2D(): Specify pixel data to generate 2D Texture
  glUniform1i(): 
  glDrawArrays(): draw.
  glutSwapBuffers(): show.
```
