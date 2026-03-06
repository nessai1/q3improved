/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
/*
** LINUX_GLIMP.C -- GLFW backend
**
** Window management, GL context, and input via GLFW.
** Replaces the X11/GLX/XF86VidMode code.
*/

#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>

#include <GLFW/glfw3.h>

#include "../renderer/tr_local.h"
#include "../client/client.h"
#include "linux_local.h"
#include "unix_glw.h"

typedef enum {
  RSERR_OK,
  RSERR_INVALID_FULLSCREEN,
  RSERR_INVALID_MODE,
  RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

static GLFWwindow *glfw_window = NULL;
static GLFWmonitor *glfw_monitor = NULL;

static qboolean mouse_avail = qfalse;
static qboolean mouse_active = qfalse;
static double mouse_last_x, mouse_last_y;
static qboolean mouse_first = qtrue;

// developer feature, allows to break without losing mouse pointer
cvar_t *in_nograb;
cvar_t *in_mouse;
cvar_t *in_subframe;
cvar_t *in_joystick      = NULL;
cvar_t *in_joystickDebug = NULL;
cvar_t *joy_threshold    = NULL;
cvar_t *r_allowSoftwareGL;

static int win_x, win_y;

// gamma state
static unsigned short initial_ramp[3][256];
static qboolean gamma_saved = qfalse;

// forward declarations
void IN_ActivateMouse( void );
void IN_DeactivateMouse( void );

// ========================================================================
// Key translation: GLFW_KEY_* -> K_*
// ========================================================================

static int GLFWKeyToQ3Key( int key )
{
  switch ( key )
  {
  case GLFW_KEY_ESCAPE:       return K_ESCAPE;
  case GLFW_KEY_ENTER:        return K_ENTER;
  case GLFW_KEY_TAB:          return K_TAB;
  case GLFW_KEY_BACKSPACE:    return K_BACKSPACE;
  case GLFW_KEY_SPACE:        return K_SPACE;

  case GLFW_KEY_UP:           return K_UPARROW;
  case GLFW_KEY_DOWN:         return K_DOWNARROW;
  case GLFW_KEY_LEFT:         return K_LEFTARROW;
  case GLFW_KEY_RIGHT:        return K_RIGHTARROW;

  case GLFW_KEY_LEFT_SHIFT:
  case GLFW_KEY_RIGHT_SHIFT:  return K_SHIFT;
  case GLFW_KEY_LEFT_CONTROL:
  case GLFW_KEY_RIGHT_CONTROL: return K_CTRL;
  case GLFW_KEY_LEFT_ALT:
  case GLFW_KEY_RIGHT_ALT:
  case GLFW_KEY_LEFT_SUPER:
  case GLFW_KEY_RIGHT_SUPER:  return K_ALT;

  case GLFW_KEY_INSERT:       return K_INS;
  case GLFW_KEY_DELETE:       return K_DEL;
  case GLFW_KEY_PAGE_UP:      return K_PGUP;
  case GLFW_KEY_PAGE_DOWN:    return K_PGDN;
  case GLFW_KEY_HOME:         return K_HOME;
  case GLFW_KEY_END:          return K_END;

  case GLFW_KEY_F1:           return K_F1;
  case GLFW_KEY_F2:           return K_F2;
  case GLFW_KEY_F3:           return K_F3;
  case GLFW_KEY_F4:           return K_F4;
  case GLFW_KEY_F5:           return K_F5;
  case GLFW_KEY_F6:           return K_F6;
  case GLFW_KEY_F7:           return K_F7;
  case GLFW_KEY_F8:           return K_F8;
  case GLFW_KEY_F9:           return K_F9;
  case GLFW_KEY_F10:          return K_F10;
  case GLFW_KEY_F11:          return K_F11;
  case GLFW_KEY_F12:          return K_F12;

  case GLFW_KEY_PAUSE:        return K_PAUSE;

  case GLFW_KEY_KP_0:         return K_KP_INS;
  case GLFW_KEY_KP_1:         return K_KP_END;
  case GLFW_KEY_KP_2:         return K_KP_DOWNARROW;
  case GLFW_KEY_KP_3:         return K_KP_PGDN;
  case GLFW_KEY_KP_4:         return K_KP_LEFTARROW;
  case GLFW_KEY_KP_5:         return K_KP_5;
  case GLFW_KEY_KP_6:         return K_KP_RIGHTARROW;
  case GLFW_KEY_KP_7:         return K_KP_HOME;
  case GLFW_KEY_KP_8:         return K_KP_UPARROW;
  case GLFW_KEY_KP_9:         return K_KP_PGUP;
  case GLFW_KEY_KP_ENTER:     return K_KP_ENTER;
  case GLFW_KEY_KP_ADD:       return K_KP_PLUS;
  case GLFW_KEY_KP_SUBTRACT:  return K_KP_MINUS;
  case GLFW_KEY_KP_MULTIPLY:  return '*';
  case GLFW_KEY_KP_DIVIDE:    return K_KP_SLASH;
  case GLFW_KEY_KP_DECIMAL:   return K_KP_DEL;

  // printable keys: GLFW uses ASCII values for A-Z, 0-9, etc.
  default:
    if ( key >= GLFW_KEY_A && key <= GLFW_KEY_Z )
      return 'a' + ( key - GLFW_KEY_A );
    if ( key >= GLFW_KEY_0 && key <= GLFW_KEY_9 )
      return '0' + ( key - GLFW_KEY_0 );
    if ( key == GLFW_KEY_MINUS )          return '-';
    if ( key == GLFW_KEY_EQUAL )          return '=';
    if ( key == GLFW_KEY_LEFT_BRACKET )   return '[';
    if ( key == GLFW_KEY_RIGHT_BRACKET )  return ']';
    if ( key == GLFW_KEY_BACKSLASH )      return '\\';
    if ( key == GLFW_KEY_SEMICOLON )      return ';';
    if ( key == GLFW_KEY_APOSTROPHE )     return '\'';
    if ( key == GLFW_KEY_GRAVE_ACCENT )   return '~';
    if ( key == GLFW_KEY_COMMA )          return ',';
    if ( key == GLFW_KEY_PERIOD )         return '.';
    if ( key == GLFW_KEY_SLASH )          return '/';
    break;
  }
  return 0;
}

// ========================================================================
// GLFW Callbacks
// ========================================================================

static void key_callback( GLFWwindow *w, int key, int scancode, int action, int mods )
{
  int q3key;
  int t = Sys_Milliseconds();

  if ( action == GLFW_REPEAT )
    return;  // engine handles repeat at a higher level

  q3key = GLFWKeyToQ3Key( key );
  if ( q3key )
    Sys_QueEvent( t, SE_KEY, q3key, ( action == GLFW_PRESS ), 0, NULL );
}

static void char_callback( GLFWwindow *w, unsigned int codepoint )
{
  int t = Sys_Milliseconds();
  if ( codepoint < 256 )
    Sys_QueEvent( t, SE_CHAR, (int)codepoint, 0, 0, NULL );
}

static void cursor_pos_callback( GLFWwindow *w, double xpos, double ypos )
{
  int t = Sys_Milliseconds();
  int dx, dy;

  if ( !mouse_active )
    return;

  if ( mouse_first ) {
    mouse_last_x = xpos;
    mouse_last_y = ypos;
    mouse_first = qfalse;
    return;
  }

  dx = (int)( xpos - mouse_last_x );
  dy = (int)( ypos - mouse_last_y );
  mouse_last_x = xpos;
  mouse_last_y = ypos;

  if ( dx || dy )
    Sys_QueEvent( t, SE_MOUSE, dx, dy, 0, NULL );
}

static void mouse_button_callback( GLFWwindow *w, int button, int action, int mods )
{
  int t = Sys_Milliseconds();
  int q3button;

  switch ( button )
  {
  case GLFW_MOUSE_BUTTON_LEFT:   q3button = K_MOUSE1; break;
  case GLFW_MOUSE_BUTTON_RIGHT:  q3button = K_MOUSE2; break;
  case GLFW_MOUSE_BUTTON_MIDDLE: q3button = K_MOUSE3; break;
  case GLFW_MOUSE_BUTTON_4:      q3button = K_MOUSE4; break;
  case GLFW_MOUSE_BUTTON_5:      q3button = K_MOUSE5; break;
  default: return;
  }

  Sys_QueEvent( t, SE_KEY, q3button, ( action == GLFW_PRESS ), 0, NULL );
}

static void scroll_callback( GLFWwindow *w, double xoffset, double yoffset )
{
  int t = Sys_Milliseconds();

  if ( yoffset > 0 ) {
    Sys_QueEvent( t, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
    Sys_QueEvent( t, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
  } else if ( yoffset < 0 ) {
    Sys_QueEvent( t, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
    Sys_QueEvent( t, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
  }
}

static void window_focus_callback( GLFWwindow *w, int focused )
{
  if ( focused ) {
    if ( mouse_avail && !in_nograb->value )
      IN_ActivateMouse();
  } else {
    IN_DeactivateMouse();
  }
}

static void window_pos_callback( GLFWwindow *w, int xpos, int ypos )
{
  win_x = xpos;
  win_y = ypos;
}

static void glfw_error_callback( int error, const char *description )
{
  ri.Printf( PRINT_ALL, "GLFW Error %d: %s\n", error, description );
}

// ========================================================================
// Mouse grab/ungrab
// ========================================================================

void IN_ActivateMouse( void )
{
  if ( !mouse_avail || !glfw_window )
    return;

  if ( !mouse_active ) {
    if ( !in_nograb->value ) {
      glfwSetInputMode( glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED );
      if ( glfwRawMouseMotionSupported() )
        glfwSetInputMode( glfw_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE );
    }
    mouse_first = qtrue;
    mouse_active = qtrue;
  }
}

void IN_DeactivateMouse( void )
{
  if ( !mouse_avail || !glfw_window )
    return;

  if ( mouse_active ) {
    glfwSetInputMode( glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL );
    glfwSetInputMode( glfw_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE );
    mouse_active = qfalse;
  }
}

// ========================================================================
// Gamma
// ========================================================================

static void GLW_InitGamma( void )
{
  const GLFWgammaramp *ramp;

  glConfig.deviceSupportsGamma = qfalse;
  glfw_monitor = glfwGetPrimaryMonitor();
  if ( !glfw_monitor )
    return;

  ramp = glfwGetGammaRamp( glfw_monitor );
  if ( ramp && ramp->size == 256 ) {
    memcpy( initial_ramp[0], ramp->red,   256 * sizeof(unsigned short) );
    memcpy( initial_ramp[1], ramp->green, 256 * sizeof(unsigned short) );
    memcpy( initial_ramp[2], ramp->blue,  256 * sizeof(unsigned short) );
    gamma_saved = qtrue;
    glConfig.deviceSupportsGamma = qtrue;
  }
}

void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
  GLFWgammaramp ramp;
  unsigned short table[3][256];
  int i;

  if ( !glConfig.deviceSupportsGamma || !glfw_monitor )
    return;

  for ( i = 0; i < 256; i++ ) {
    table[0][i] = ( (unsigned short)red[i] ) << 8;
    table[1][i] = ( (unsigned short)green[i] ) << 8;
    table[2][i] = ( (unsigned short)blue[i] ) << 8;
  }

  ramp.size = 256;
  ramp.red   = table[0];
  ramp.green = table[1];
  ramp.blue  = table[2];
  glfwSetGammaRamp( glfw_monitor, &ramp );
}

// ========================================================================
// GL Extensions
// ========================================================================

static void GLW_InitExtensions( void )
{
  ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

  // GL_S3_s3tc
  if ( strstr( glConfig.extensions_string, "GL_S3_s3tc" ) ) {
    if ( r_ext_compressed_textures->value )
      glConfig.textureCompression = TC_S3TC;
    else
      glConfig.textureCompression = TC_NONE;
  } else {
    glConfig.textureCompression = TC_NONE;
  }

  // GL_EXT_texture_env_add
  glConfig.textureEnvAddAvailable = qfalse;
  if ( strstr( glConfig.extensions_string, "EXT_texture_env_add" ) ) {
    if ( r_ext_texture_env_add->integer )
      glConfig.textureEnvAddAvailable = qtrue;
    else
      glConfig.textureEnvAddAvailable = qfalse;
    ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
  } else {
    ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
  }

  // GL_ARB_multitexture
  qglMultiTexCoord2fARB = NULL;
  qglActiveTextureARB = NULL;
  qglClientActiveTextureARB = NULL;
  if ( strstr( glConfig.extensions_string, "GL_ARB_multitexture" ) ) {
    if ( r_ext_multitexture->value ) {
      qglMultiTexCoord2fARB = ( void ( APIENTRY *)( GLenum, GLfloat, GLfloat ) )
        dlsym( glw_state.OpenGLLib, "glMultiTexCoord2fARB" );
      qglActiveTextureARB = ( void ( APIENTRY *)( GLenum ) )
        dlsym( glw_state.OpenGLLib, "glActiveTextureARB" );
      qglClientActiveTextureARB = ( void ( APIENTRY *)( GLenum ) )
        dlsym( glw_state.OpenGLLib, "glClientActiveTextureARB" );

      if ( qglActiveTextureARB ) {
        qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &glConfig.maxActiveTextures );
        if ( glConfig.maxActiveTextures > 1 )
          ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
        else
          ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
      }
    } else {
      ri.Printf( PRINT_ALL, "...ignoring GL_ARB_multitexture\n" );
    }
  } else {
    ri.Printf( PRINT_ALL, "...GL_ARB_multitexture not found\n" );
  }

  // GL_EXT_compiled_vertex_array
  if ( strstr( glConfig.extensions_string, "GL_EXT_compiled_vertex_array" ) ) {
    if ( r_ext_compiled_vertex_array->value ) {
      ri.Printf( PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n" );
      qglLockArraysEXT = ( void ( APIENTRY *)( GLint, GLint ) )
        dlsym( glw_state.OpenGLLib, "glLockArraysEXT" );
      qglUnlockArraysEXT = ( void ( APIENTRY *)( void ) )
        dlsym( glw_state.OpenGLLib, "glUnlockArraysEXT" );
      if ( !qglLockArraysEXT || !qglUnlockArraysEXT ) {
        ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
      }
    } else {
      ri.Printf( PRINT_ALL, "...ignoring GL_EXT_compiled_vertex_array\n" );
    }
  } else {
    ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
  }
}

// ========================================================================
// Window / GL context
// ========================================================================

static int GLW_SetMode( int mode, qboolean fullscreen )
{
  int colorbits, depthbits, stencilbits;

  ri.Printf( PRINT_ALL, "Initializing OpenGL display\n" );
  ri.Printf( PRINT_ALL, "...setting mode %d:", mode );

  if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, mode ) ) {
    ri.Printf( PRINT_ALL, " invalid mode\n" );
    return RSERR_INVALID_MODE;
  }
  ri.Printf( PRINT_ALL, " %d %d\n", glConfig.vidWidth, glConfig.vidHeight );

  // color/depth/stencil bits
  colorbits = ( r_colorbits->value == 0 ) ? 24 : r_colorbits->value;
  depthbits = ( r_depthbits->value == 0 ) ? 24 : r_depthbits->value;
  stencilbits = r_stencilbits->value;

  glfwWindowHint( GLFW_RED_BITS, colorbits / 3 );
  glfwWindowHint( GLFW_GREEN_BITS, colorbits / 3 );
  glfwWindowHint( GLFW_BLUE_BITS, colorbits / 3 );
  glfwWindowHint( GLFW_DEPTH_BITS, depthbits );
  glfwWindowHint( GLFW_STENCIL_BITS, stencilbits );
  glfwWindowHint( GLFW_DOUBLEBUFFER, GLFW_TRUE );

  glConfig.colorBits = colorbits;
  glConfig.depthBits = depthbits;
  glConfig.stencilBits = stencilbits;

  // create window
  GLFWmonitor *monitor = NULL;
  if ( fullscreen ) {
    monitor = glfwGetPrimaryMonitor();
    if ( !monitor ) {
      ri.Printf( PRINT_ALL, "...no monitor found, falling back to windowed\n" );
      fullscreen = qfalse;
    }
  }

  glfw_window = glfwCreateWindow( glConfig.vidWidth, glConfig.vidHeight,
                                   "Quake III Arena", monitor, NULL );
  if ( !glfw_window ) {
    ri.Printf( PRINT_ALL, "...glfwCreateWindow failed\n" );
    return fullscreen ? RSERR_INVALID_FULLSCREEN : RSERR_INVALID_MODE;
  }

  glfwMakeContextCurrent( glfw_window );
  glConfig.isFullscreen = fullscreen;

  // set up callbacks
  glfwSetKeyCallback( glfw_window, key_callback );
  glfwSetCharCallback( glfw_window, char_callback );
  glfwSetCursorPosCallback( glfw_window, cursor_pos_callback );
  glfwSetMouseButtonCallback( glfw_window, mouse_button_callback );
  glfwSetScrollCallback( glfw_window, scroll_callback );
  glfwSetWindowFocusCallback( glfw_window, window_focus_callback );
  glfwSetWindowPosCallback( glfw_window, window_pos_callback );

  ri.Printf( PRINT_ALL, "...GL window created\n" );
  return RSERR_OK;
}

static qboolean GLW_StartDriverAndSetMode( int mode, qboolean fullscreen )
{
  rserr_t err;

  if ( fullscreen && in_nograb->value ) {
    ri.Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n" );
    ri.Cvar_Set( "r_fullscreen", "0" );
    r_fullscreen->modified = qfalse;
    fullscreen = qfalse;
  }

  err = (rserr_t)GLW_SetMode( mode, fullscreen );

  switch ( err ) {
  case RSERR_INVALID_FULLSCREEN:
    ri.Printf( PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n" );
    return qfalse;
  case RSERR_INVALID_MODE:
    ri.Printf( PRINT_ALL, "...WARNING: could not set the given mode (%d)\n", mode );
    return qfalse;
  default:
    break;
  }
  return qtrue;
}

static qboolean GLW_LoadOpenGL( void )
{
  // QGL_Init loads all GL function pointers via dlsym
  if ( QGL_Init( OPENGL_DRIVER_NAME ) ) {
    return qtrue;
  }
  return qfalse;
}

// ========================================================================
// Public GLimp interface
// ========================================================================

void GLimp_Init( void )
{
  char buf[1024];
  cvar_t *lastValidRenderer = ri.Cvar_Get( "r_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE );

  r_allowSoftwareGL = ri.Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );

  InitSig();

  glfwSetErrorCallback( glfw_error_callback );

  if ( !glfwInit() ) {
    ri.Error( ERR_FATAL, "GLimp_Init() - glfwInit() failed\n" );
  }

  // load GL function pointers and create window
  if ( !GLW_StartDriverAndSetMode( r_mode->integer, (qboolean)r_fullscreen->integer ) ) {
    // try default mode
    if ( r_mode->integer != 3 ) {
      if ( !GLW_StartDriverAndSetMode( 3, qfalse ) ) {
        ri.Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem\n" );
      }
    } else {
      ri.Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem\n" );
    }
  }

  // load GL function pointers (dlsym from libGL)
  if ( !GLW_LoadOpenGL() ) {
    ri.Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem\n" );
  }

  glConfig.driverType = GLDRV_ICD;
  glConfig.hardwareType = GLHW_GENERIC;

  // get config strings
  Q_strncpyz( glConfig.vendor_string, (const char *)qglGetString( GL_VENDOR ), sizeof( glConfig.vendor_string ) );
  Q_strncpyz( glConfig.renderer_string, (const char *)qglGetString( GL_RENDERER ), sizeof( glConfig.renderer_string ) );
  if ( *glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n' )
    glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
  Q_strncpyz( glConfig.version_string, (const char *)qglGetString( GL_VERSION ), sizeof( glConfig.version_string ) );
  Q_strncpyz( glConfig.extensions_string, (const char *)qglGetString( GL_EXTENSIONS ), sizeof( glConfig.extensions_string ) );

  // chipset specific configuration
  strcpy( buf, glConfig.renderer_string );
  Q_strlwr( buf );

  if ( Q_stricmp( lastValidRenderer->string, glConfig.renderer_string ) ) {
    glConfig.hardwareType = GLHW_GENERIC;
    ri.Cvar_Set( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST" );
  }

  ri.Cvar_Set( "r_lastValidRenderer", glConfig.renderer_string );

  GLW_InitExtensions();
  GLW_InitGamma();

  InitSig();
}

void GLimp_Shutdown( void )
{
  IN_DeactivateMouse();

  if ( gamma_saved && glfw_monitor ) {
    GLFWgammaramp ramp;
    ramp.size = 256;
    ramp.red   = initial_ramp[0];
    ramp.green = initial_ramp[1];
    ramp.blue  = initial_ramp[2];
    glfwSetGammaRamp( glfw_monitor, &ramp );
  }

  if ( glfw_window ) {
    glfwDestroyWindow( glfw_window );
    glfw_window = NULL;
  }

  glfwTerminate();

  QGL_Shutdown();

  memset( &glConfig, 0, sizeof( glConfig ) );
  memset( &glState, 0, sizeof( glState ) );
}

void GLimp_LogComment( char *comment )
{
  if ( glw_state.log_fp ) {
    fprintf( glw_state.log_fp, "%s", comment );
  }
}

void GLimp_EndFrame( void )
{
  if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 ) {
    glfwSwapBuffers( glfw_window );
  }
  QGL_EnableLogging( (qboolean)r_logFile->integer );
}

// ========================================================================
// SMP acceleration
// ========================================================================

#ifdef SMP

static pthread_mutex_t  smpMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   renderCommandsEvent = PTHREAD_COND_INITIALIZER;
static pthread_cond_t   renderCompletedEvent = PTHREAD_COND_INITIALIZER;
static void (*glimpRenderThread)( void );
static volatile void    *smpData = NULL;
static volatile qboolean smpDataReady;

static void *GLimp_RenderThreadWrapper( void *arg )
{
  Com_Printf( "Render thread starting\n" );
  glimpRenderThread();
  glfwMakeContextCurrent( NULL );
  Com_Printf( "Render thread terminating\n" );
  return arg;
}

qboolean GLimp_SpawnRenderThread( void (*function)( void ) )
{
  pthread_t renderThread;
  int ret;

  pthread_mutex_init( &smpMutex, NULL );
  pthread_cond_init( &renderCommandsEvent, NULL );
  pthread_cond_init( &renderCompletedEvent, NULL );

  glimpRenderThread = function;

  ret = pthread_create( &renderThread, NULL, GLimp_RenderThreadWrapper, NULL );
  if ( ret ) {
    ri.Printf( PRINT_ALL, "pthread_create returned %d: %s", ret, strerror( ret ) );
    return qfalse;
  } else {
    ret = pthread_detach( renderThread );
    if ( ret ) {
      ri.Printf( PRINT_ALL, "pthread_detach returned %d: %s", ret, strerror( ret ) );
    }
  }
  return qtrue;
}

void *GLimp_RendererSleep( void )
{
  void *data;

  glfwMakeContextCurrent( NULL );

  pthread_mutex_lock( &smpMutex );
  {
    smpData = NULL;
    smpDataReady = qfalse;
    pthread_cond_signal( &renderCompletedEvent );
    while ( !smpDataReady ) {
      pthread_cond_wait( &renderCommandsEvent, &smpMutex );
    }
    data = (void *)smpData;
  }
  pthread_mutex_unlock( &smpMutex );

  glfwMakeContextCurrent( glfw_window );

  return data;
}

void GLimp_FrontEndSleep( void )
{
  pthread_mutex_lock( &smpMutex );
  {
    while ( smpData ) {
      pthread_cond_wait( &renderCompletedEvent, &smpMutex );
    }
  }
  pthread_mutex_unlock( &smpMutex );

  glfwMakeContextCurrent( glfw_window );
}

void GLimp_WakeRenderer( void *data )
{
  glfwMakeContextCurrent( NULL );

  pthread_mutex_lock( &smpMutex );
  {
    assert( smpData == NULL );
    smpData = data;
    smpDataReady = qtrue;
    pthread_cond_signal( &renderCommandsEvent );
  }
  pthread_mutex_unlock( &smpMutex );
}

#else

void GLimp_RenderThreadWrapper( void *stub ) {}
qboolean GLimp_SpawnRenderThread( void (*function)( void ) ) {
  ri.Printf( PRINT_WARNING, "ERROR: SMP support was disabled at compile time\n" );
  return qfalse;
}
void *GLimp_RendererSleep( void ) { return NULL; }
void GLimp_FrontEndSleep( void ) {}
void GLimp_WakeRenderer( void *data ) {}

#endif

// ========================================================================
// Input
// ========================================================================

void IN_Init( void )
{
  Com_Printf( "\n------- Input Initialization -------\n" );

  in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
  in_subframe = Cvar_Get( "in_subframe", "1", CVAR_ARCHIVE );
  in_nograb = Cvar_Get( "in_nograb", "0", 0 );
  in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE | CVAR_LATCH );
  in_joystickDebug = Cvar_Get( "in_debugjoystick", "0", CVAR_TEMP );
  joy_threshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );

  if ( in_mouse->value )
    mouse_avail = qtrue;
  else
    mouse_avail = qfalse;

  IN_StartupJoystick();
  Com_Printf( "------------------------------------\n" );
}

void IN_Shutdown( void )
{
  IN_DeactivateMouse();
  mouse_avail = qfalse;
}

void IN_Frame( void )
{
  IN_JoyMove();

  if ( cls.keyCatchers & KEYCATCH_CONSOLE ) {
    if ( Cvar_VariableValue( "r_fullscreen" ) == 0 ) {
      IN_DeactivateMouse();
      return;
    }
  }

  IN_ActivateMouse();
}

void IN_Activate( void )
{
}

void Sys_SendKeyEvents( void )
{
  if ( !glfw_window )
    return;
  glfwPollEvents();
}

void KBD_Init( void ) {}
void KBD_Close( void ) {}
