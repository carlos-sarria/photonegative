#ifdef GEGL_PROPERTIES

property_double (film_contrast, _("Film Contrast"), 1.4)
    value_range (0.50, 4.0)
    description   (_("Contrast of the film negative. Higher values give more separation between shadows and highlights."))

property_boolean (preserve_hue, _("Preserve Hue"), FALSE)
    description   (_("When enabled, preserves the original hue of each pixel and inverts only the luminance. "
                     "The orange mask is disabled in this mode."))

property_double (mask_strength, _("Orange Mask"), 1.0)
    value_range (0.0, 2.0)
    description   (_("Strength of the orange/amber mask characteristic of color negative film. Set to 0 for no mask. "
                     "Only applies when Preserve Hue is disabled."))

property_double (crossover, _("Color Crossover"), 0.15)
    value_range (0.0, 0.5)
    description   (_("Per-channel contrast variation to simulate real film emulsion layers. Higher values give more color crossover. "
                     "Only applies when Preserve Hue is disabled."))

#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_NAME     photonegative
#define GEGL_OP_C_SOURCE photonegative.c

// Fix for wrong gettext on Windows
#define _(x) x

#include "gegl-op.h"
#include <math.h>

/*
 * Perceptual luminance coefficients (Rec. 709 / sRGB).
 * These match how babl weights R~G~B~ perceptual channels
 * for the luma-like component.
 */
#define LUMA_R 0.2126f
#define LUMA_G 0.7152f
#define LUMA_B 0.0722f

static gboolean
process (GeglOperation       *op,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi,
         gint                 level)
{
    gfloat *GEGL_ALIGNED in_pixel  = in_buf;
    gfloat *GEGL_ALIGNED out_pixel = out_buf;

    gfloat film_contrast = GEGL_PROPERTIES (op)->film_contrast;
    gboolean preserve_hue_flag = GEGL_PROPERTIES (op)->preserve_hue;
    gfloat mask_strength = GEGL_PROPERTIES (op)->mask_strength;
    gfloat crossover     = GEGL_PROPERTIES (op)->crossover;

    /*
     * Orange mask — the characteristic orange/amber base of C-41 film.
     * These values represent the transmittance (clear = high, dense = low)
     * of the undeveloped film base for each color channel.
     */
    const float mask_r = 1.0f - mask_strength * 0.08f;  /* 0.92 at full strength */
    const float mask_g = 1.0f - mask_strength * 0.42f;  /* 0.58 at full strength */
    const float mask_b = 1.0f - mask_strength * 0.78f;  /* 0.22 at full strength */

    /*
     * Per-channel gamma to simulate emulsion layer crossover.
     */
    const float gamma_r = film_contrast * (1.0f + crossover);
    const float gamma_g = film_contrast;
    const float gamma_b = film_contrast * (1.0f - crossover);

    const float epsilon = 1e-8f;

    for (glong i = 0; i < n_pixels; i++)
    {
        /*
         * Work in perceptual (R~G~B~) space.
         *
         * In perceptual space, 0.0 = black, 1.0 = white, and
         * 0.5 ≈ perceptual middle gray (≈18% linear).
         */
        float r = fmaxf (in_pixel[0], epsilon);
        float g = fmaxf (in_pixel[1], epsilon);
        float b = fmaxf (in_pixel[2], epsilon);

        if (preserve_hue_flag)
        {
            /*
             * Hue-preserving mode:
             *
             * 1. Compute the perceptual luminance (luma) of the original pixel.
             * 2. Invert only the luminance using the film contrast curve:
             *       Y_inv = pow(1.0 - Y, film_contrast)
             * 3. Scale the original RGB by Y_inv / Y to preserve the
             *    original hue (RGB ratios) while applying the inverted
             *    luminance.
             *
             * This gives a "luminance-only negative" effect — the
             * colors stay the same, only their brightness is inverted
             * with a film-like response curve.
             */
            float Y = LUMA_R * r + LUMA_G * g + LUMA_B * b;
            float Y_inv = powf (1.0f - Y, film_contrast);

            if (Y > epsilon)
            {
                float scale = Y_inv / Y;
                out_pixel[0] = scale * r;
                out_pixel[1] = scale * g;
                out_pixel[2] = scale * b;
            }
            else
            {
                /* Pure black pixel — no hue to preserve, output the inverted luma */
                out_pixel[0] = Y_inv;
                out_pixel[1] = Y_inv;
                out_pixel[2] = Y_inv;
            }
        }
        else
        {
            /*
             * Full photographic negative mode:
             *
             *   out[c] = mask[c] * pow(1.0 - in[c], gamma[c])
             *
             * This models the characteristic density curve, orange mask,
             * and per-channel crossover of real C-41 color negative film.
             */
            out_pixel[0] = mask_r * powf (1.0f - r, gamma_r);
            out_pixel[1] = mask_g * powf (1.0f - g, gamma_g);
            out_pixel[2] = mask_b * powf (1.0f - b, gamma_b);
        }

        in_pixel  += 3;
        out_pixel += 3;
    }

    return TRUE;
}

static void prepare
(GeglOperation *operation)
{
    const Babl *space = gegl_operation_get_source_space (operation, "input");

    /*
     * Use perceptual (R~G~B~) encoding.
     *
     * The tilde (~) in babl notation means the channel follows the
     * source space's perceptual TRC (typically sRGB gamma or similar).
     * This is important because a photographic negative's density is
     * approximately logarithmic with respect to exposure, and perceptual
     * encoding approximates a log-like response.  Working in perceptual
     * space gives a natural-looking inversion that maps well to how
     * we perceive "darker" and "lighter" in the negative.
     */
    gegl_operation_set_format (operation, "input",  babl_format_with_space ("R~G~B~ float", space));
    gegl_operation_set_format (operation, "output", babl_format_with_space ("R~G~B~ float", space));
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass            *operation_class    = GEGL_OPERATION_CLASS (klass);
  GeglOperationPointFilterClass *point_filter_class = GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  operation_class->prepare    = prepare;
  point_filter_class->process = process;

  gegl_operation_class_set_keys (operation_class,
                                 "title",         "Photographic Negative",
                                 "name",          "cs:photonegative",
                                 "description",   _("Simulate a photographic color negative with orange mask and film contrast. "
                                                     "Models the characteristic density curve and orange base mask of C-41 "
                                                     "color negative film.  When 'Preserve Hue' is enabled, only the luminance "
                                                     "is inverted while preserving the original hue."),
                                 "gimp:menu-path", "<Image>/Colors",
                                 "gimp:menu-label", _("Photographic Negative"),
                                 NULL);
}

#endif