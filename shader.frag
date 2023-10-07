#ifdef GL_ES
precision highp float;
#endif

varying vec2 v_texCoord;

uniform sampler2D y_texture;
uniform sampler2D uv_texture;

uniform sampler2D text_rgba;

uniform bool is_rgba;

void main (void)
{
    float r, g, b, y, u, v;
    vec4 rgba;
    
    if (!is_rgba)
    {
        y = texture2D(y_texture, v_texCoord).r;
        u = texture2D(uv_texture, v_texCoord).r - 0.5;
        v = texture2D(uv_texture, v_texCoord).a - 0.5;

        //The numbers are just YUV to RGB conversion constants
        r = y + 1.13983*v;
        g = y - 0.39465*u - 0.58060*v;
        b = y + 2.03211*u;

        //We finally set the RGB color of our pixel
        gl_FragColor = vec4(r, g, b, 1.0);;
    }
    else
    {
        rgba = texture2D(text_rgba, v_texCoord);
        if (rgba.a != 0.0 && rgba.r > 0.6)
        {
            gl_FragColor = rgba;
        }
        else
        {
            discard;
        }
    }
}
