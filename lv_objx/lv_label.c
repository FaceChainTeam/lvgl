/**
 * @file lv_rect.c
 * 
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_conf.h"
#if USE_LV_LABEL != 0

#include "misc/gfx/color.h"
#include "misc/gfx/text.h"
#include "misc/math/math_base.h"
#include "lv_label.h"
#include "../lv_obj/lv_obj.h"
#include "../lv_obj/lv_group.h"
#include "misc/gfx/text.h"
#include "misc/gfx/anim.h"
#include "../lv_draw/lv_draw.h"

/*********************
 *      DEFINES
 *********************/
/*Test configurations*/
#ifndef LV_LABEL_SCROLL_SPEED
#define LV_LABEL_SCROLL_SPEED       (25 << LV_ANTIALIAS) /*Hor, or ver. scroll speed (px/sec) in 'LV_LABEL_LONG_SCROLL' mode*/
#endif

#ifndef LV_LABEL_SCROLL_SPEED_VER
#define LV_LABEL_SCROLL_SPEED_VER   (10 << LV_ANTIALIAS) /*Ver. scroll speed if hor. scroll is applied too*/
#endif

#ifndef LV_LABEL_SCROLL_PLAYBACK_PAUSE
#define LV_LABEL_SCROLL_PLAYBACK_PAUSE  500 /*Wait before the scroll turns back in ms*/
#endif

#ifndef LV_LABEL_SCROLL_REPEAT_PAUSE
#define LV_LABEL_SCROLL_REPEAT_PAUSE    500 /*Wait before the scroll begins again in ms*/
#endif

#define LV_LABEL_DOT_NUM	3
#define LV_LABEL_DOT_END_INV 0xFFFF

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static bool lv_label_design(lv_obj_t * label, const area_t * mask, lv_design_mode_t mode);
static void lv_label_refr_text(lv_obj_t * label);
static void lv_label_set_offset_x(lv_obj_t * label, cord_t x);
static void lv_label_set_offset_y(lv_obj_t * label, cord_t y);

/**********************
 *  STATIC VARIABLES
 **********************/
/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Create a label objects
 * @param par pointer to an object, it will be the parent of the new label
 * @param copy pointer to a button object, if not NULL then the new object will be copied from it
 * @return pointer to the created button
 */
lv_obj_t * lv_label_create(lv_obj_t * par, lv_obj_t * copy)
{
    /*Create a basic object*/
    lv_obj_t * new_label = lv_obj_create(par, copy);
    dm_assert(new_label);
    
    /*Extend the basic object to a label object*/
    lv_obj_allocate_ext_attr(new_label, sizeof(lv_label_ext_t));
    
    lv_label_ext_t * ext = lv_obj_get_ext_attr(new_label);
    dm_assert(ext);
    ext->txt = NULL;
    ext->static_txt = 0;
    ext->recolor = 0;
    ext->no_break = 0;
    ext->body_draw = 0;
    ext->align = LV_LABEL_ALIGN_LEFT;
    ext->dot_end = LV_LABEL_DOT_END_INV;
    ext->long_mode = LV_LABEL_LONG_EXPAND;
    ext->offset.x = 0;
    ext->offset.y = 0;
	lv_obj_set_design_func(new_label, lv_label_design);
	lv_obj_set_signal_func(new_label, lv_label_signal);

    /*Init the new label*/
    if(copy == NULL) {
		lv_obj_set_click(new_label, false);
		lv_obj_set_style(new_label, NULL);
		lv_label_set_long_mode(new_label, LV_LABEL_LONG_EXPAND);
		lv_label_set_text(new_label, "Text");
    }
    /*Copy 'copy' if not NULL*/
    else {
        lv_label_ext_t * copy_ext = lv_obj_get_ext_attr(copy);
        lv_label_set_long_mode(new_label, lv_label_get_long_mode(copy));
        lv_label_set_recolor(new_label, lv_label_get_recolor(copy));
        lv_label_set_body_draw(new_label, lv_label_get_body_draw(copy));
        lv_label_set_align(new_label, lv_label_get_align(copy));
        if(copy_ext->static_txt == 0) lv_label_set_text(new_label, lv_label_get_text(copy));
        else lv_label_set_text_static(new_label, lv_label_get_text(copy));

        /*Refresh the style with new signal function*/
        lv_obj_refresh_style(new_label);
    }
    return new_label;
}


/**
 * Signal function of the label
 * @param label pointer to a label object
 * @param sign a signal type from lv_signal_t enum
 * @param param pointer to a signal specific variable
 */
bool lv_label_signal(lv_obj_t * label, lv_signal_t sign, void * param)
{
    bool valid;

    /* Include the ancient signal function */
    valid = lv_obj_signal(label, sign, param);

    /* The object can be deleted so check its validity and then
     * make the object specific signal handling */
    if(valid != false) {
        lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
        if(sign ==  LV_SIGNAL_CLEANUP) {
            if(ext->static_txt == 0) {
                dm_free(ext->txt);
                ext->txt = NULL;
            }
        }
        else if(sign == LV_SIGNAL_STYLE_CHG) {
            	lv_label_refr_text(label);
        }
        else if (sign == LV_SIGNAL_CORD_CHG) {
            if(area_get_width(&label->coords) != area_get_width(param) ||
               area_get_height(&label->coords) != area_get_height(param))
            {
                lv_label_refr_text(label);
            }
        }

    }
    
    return valid;
}

/*=====================
 * Setter functions 
 *====================*/

/**
 * Set a new text for a label. Memory will be allocated to store the text by the label.
 * @param label pointer to a label object
 * @param text '\0' terminated character string. NULL to refresh with the current text.
 */
void lv_label_set_text(lv_obj_t * label, const char * text)
{
    lv_obj_invalidate(label);
    
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);

    /*If text is NULL then refresh */
    if(text == NULL) {
        lv_label_refr_text(label);
        return;
    }

    if(ext->txt == text) {
        /*If set its own text then reallocate it (maybe its size changed)*/
        ext->txt = dm_realloc(ext->txt, strlen(ext->txt) + 1);
    } else {
        /*Allocate space for the new text*/
        uint32_t len = strlen(text) + 1;
        if(ext->txt != NULL && ext->static_txt == 0) {
            dm_free(ext->txt);
            ext->txt = NULL;
        }

        ext->txt = dm_alloc(len);
        strcpy(ext->txt, text);
        ext->static_txt = 0;    /*Now the text is dynamically allocated*/
    }

    lv_label_refr_text(label);
}
/**
 * Set a new text for a label from a character array. The array don't has to be '\0' terminated.
 * Memory will be allocated to store the array by the label.
 * @param label pointer to a label object
 * @param array array of characters or NULL to refresh the label
 * @param size the size of 'array' in bytes
 */
void lv_label_set_text_array(lv_obj_t * label, const char * array, uint16_t size)
{
    lv_obj_invalidate(label);

    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);

    /*If trying to set its own text or the array is NULL then refresh */
    if(array == ext->txt || array == NULL) {
        lv_label_refr_text(label);
        return;
    }

    /*Allocate space for the new text*/
    if(ext->txt != NULL && ext->static_txt == 0) {
        dm_free(ext->txt);
        ext->txt = NULL;
    }
    ext->txt = dm_alloc(size + 1);
    memcpy(ext->txt, array, size);
    ext->txt[size] = '\0';
    ext->static_txt = 0;    /*Now the text is dynamically allocated*/

    lv_label_refr_text(label);
}

/**
 * Set a static text. It will not be saved by the label so the 'text' variable
 * has to be 'alive' while the label exist.
 * @param label pointer to a label object
 * @param text pointer to a text. NULL to refresh with the current text.
 */
void lv_label_set_text_static(lv_obj_t * label, const char * text)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    if(ext->static_txt == 0 && ext->txt != NULL) {
        dm_free(ext->txt);
        ext->txt = NULL;
    }

    if(text != NULL) {
        ext->static_txt = 1;
        ext->txt = (char *) text;
    }

    lv_label_refr_text(label);
}

/**
 * Insert a text to the label. The label current label text can not be static.
 * @param label pointer to label object
 * @param pos character index to insert Expressed in character index and not byte index (Different in UTF-8)
 *            0: before first char.
 *            LV_LABEL_POS_LAST: after last char.
 * @param txt pointer to the text to insert
 */
void lv_label_ins_text(lv_obj_t * label, uint32_t pos,  const char * txt)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);

    /*Can not append to static text*/
    if(ext->static_txt != 0) return;

    lv_obj_invalidate(label);

    /*Allocate space for the new text*/
    uint32_t old_len = strlen(ext->txt);
    uint32_t ins_len = strlen(txt);
    uint32_t new_len = ins_len + old_len;
    ext->txt = dm_realloc(ext->txt, new_len + 1);

    if(pos == LV_LABEL_POS_LAST) {
#if TXT_UTF8 == 0
        pos = old_len;
#else
        pos = txt_len(ext->txt);
#endif
    }

    txt_ins(ext->txt, pos, txt);

    lv_label_refr_text(label);
}

/**
 * Set the behavior of the label with longer text then the object size
 * @param label pointer to a label object
 * @param long_mode the new mode from 'lv_label_long_mode' enum.
 */
void lv_label_set_long_mode(lv_obj_t * label, lv_label_long_mode_t long_mode)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);

    /*Delete the old animation (if exists)*/
    anim_del(label, (anim_fp_t) lv_obj_set_x);
    anim_del(label, (anim_fp_t) lv_obj_set_y);
    anim_del(label, (anim_fp_t) lv_label_set_offset_x);
    anim_del(label, (anim_fp_t) lv_label_set_offset_y);
    ext->offset.x = 0;
    ext->offset.y = 0;

    if(long_mode == LV_LABEL_LONG_ROLL) ext->expand = 1;
    else ext->expand = 0;

    ext->long_mode = long_mode;
    lv_label_refr_text(label);
}


void lv_label_set_align(lv_obj_t *label, lv_label_align_t align)
{
    lv_label_ext_t *ext = lv_obj_get_ext_attr(label);

    ext->align = align;

    lv_obj_invalidate(label);       /*Enough to invalidate because alignment is only drawing related (lv_refr_label_text() not required)*/

}

/**
 * Enable the recoloring by in-line commands
 * @param label pointer to a label object
 * @param recolor_enable true: enable recoloring, false: disable
 */
void lv_label_set_recolor(lv_obj_t * label, bool recolor_enable)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);

    ext->recolor = recolor_enable == false ? 0 : 1;

    lv_label_refr_text(label);  /*Refresh the text because the potential colo codes in text needs to be hided or revealed*/
}

/**
 * Set the label to ignore (or accept) line breaks on '\n'
 * @param label pointer to a label object
 * @param no_break_enable true: ignore line breaks, false: make line breaks on '\n'
 */
void lv_label_set_no_break(lv_obj_t * label, bool no_break_enable)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    ext->no_break = no_break_enable == false ? 0 : 1;

    lv_label_refr_text(label);
}

/**
 * Set the label to draw (or not draw) background specified in its style's body
 * @param label pointer to a label object
 * @param body_enable true: draw body; false: don't draw body
 */
void lv_label_set_body_draw(lv_obj_t *label, bool body_enable)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    ext->body_draw = body_enable == false ? 0 : 1;

    lv_obj_invalidate(label);
}

/*=====================
 * Getter functions 
 *====================*/

/**
 * Get the text of a label
 * @param label pointer to a label object
 * @return the text of the label
 */
char * lv_label_get_text(lv_obj_t * label)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    
    return ext->txt;
}

/**
 * Get the long mode of a label
 * @param label pointer to a label object
 * @return the long mode
 */
lv_label_long_mode_t lv_label_get_long_mode(lv_obj_t * label)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    return ext->long_mode;
}

/**
 * Get the align attribute
 * @param label pointer to a label object
 * @return LV_LABEL_ALIGN_LEFT or LV_LABEL_ALIGN_CENTER
 */
lv_label_align_t lv_label_get_align(lv_obj_t * label)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    return ext->align;
}

/**
 * Get the recoloring attribute
 * @param label pointer to a label object
 * @return true: recoloring is enabled, false: disable
 */
bool lv_label_get_recolor(lv_obj_t * label)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    return ext->recolor == 0 ? false : true;
}

/**
 * Get the no break attribute
 * @param label pointer to a label object
 * @return true: no_break_enabled (ignore '\n' line breaks); false: make line breaks on '\n'
 */
bool lv_label_get_no_break(lv_obj_t * label)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    return ext->no_break == 0 ? false : true;
}

/**
 * Get the body draw attribute
 * @param label pointer to a label object
 * @return true: draw body; false: don't draw body
 */
bool lv_label_get_body_draw(lv_obj_t *label)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    return ext->body_draw == 0 ? false : true;
}


/**
 * Get the relative x and y coordinates of a letter
 * @param label pointer to a label object
 * @param index index of the letter [0 ... text length]. Expressed in character index, not byte index (different in UTF-8)
 * @param pos store the result here (E.g. index = 0 gives 0;0 coordinates)
 */
void lv_label_get_letter_pos(lv_obj_t * label, uint16_t index, point_t * pos)
{
	const char * txt = lv_label_get_text(label);
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    uint32_t line_start = 0;
    uint32_t new_line_start = 0;
    cord_t max_w = lv_obj_get_width(label);
    lv_style_t * style = lv_obj_get_style(label);
    const font_t * font = style->text.font;
    uint8_t letter_height = font_get_height(font) >> FONT_ANTIALIAS;
    cord_t y = 0;
    txt_flag_t flag = TXT_FLAG_NONE;

    if(ext->recolor != 0) flag |= TXT_FLAG_RECOLOR;
    if(ext->expand != 0) flag |= TXT_FLAG_EXPAND;
    if(ext->no_break != 0) flag |= TXT_FLAG_NO_BREAK;
    if(ext->align == LV_LABEL_ALIGN_CENTER) flag |= TXT_FLAG_CENTER;

    /*If the width will be expanded  the set the max length to very big */
    if(ext->long_mode == LV_LABEL_LONG_EXPAND || ext->long_mode == LV_LABEL_LONG_SCROLL) {
        max_w = CORD_MAX;
    }

    index = txt_utf8_get_byte_id(txt, index);

    /*Search the line of the index letter */;
    while (txt[new_line_start] != '\0') {
        new_line_start += txt_get_next_line(&txt[line_start], font, style->text.letter_space, max_w, flag);
        if(index < new_line_start || txt[new_line_start] == '\0') break; /*The line of 'index' letter begins at 'line_start'*/

        y += letter_height + style->text.line_space;
        line_start = new_line_start;
    }

    /*If the last character is line break then go to the next line*/
    if((txt[index - 1] == '\n' || txt[index - 1] == '\r') && txt[index] == '\0') {
        y += letter_height + style->text.line_space;
        line_start = index;
    }

    /*Calculate the x coordinate*/
    cord_t x = 0;
	uint32_t i = line_start;
    uint32_t cnt = line_start;                      /*Count the letter (in UTF-8 1 letter not 1 byte)*/
	txt_cmd_state_t cmd_state = TXT_CMD_STATE_WAIT;
	uint32_t letter;
	while(cnt < index) {
        cnt += txt_utf8_size(txt[i]);
	    letter = txt_utf8_next(txt, &i);
        /*Handle the recolor command*/
        if((flag & TXT_FLAG_RECOLOR) != 0) {
            if(txt_is_cmd(&cmd_state, txt[i]) != false) {
                continue; /*Skip the letter is it is part of a command*/
            }
        }
        x += (font_get_width(font, letter) >> FONT_ANTIALIAS) + style->text.letter_space;
	}

	if(ext->align == LV_LABEL_ALIGN_CENTER) {
		cord_t line_w;
        line_w = txt_get_width(&txt[line_start], new_line_start - line_start,
                               font, style->text.letter_space, flag);
		x += lv_obj_get_width(label) / 2 - line_w / 2;
    }

    pos->x = x;
    pos->y = y;

}

/**
 * Get the index of letter on a relative point of a label
 * @param label pointer to label object
 * @param pos pointer to point with coordinates on a the label
 * @return the index of the letter on the 'pos_p' point (E.g. on 0;0 is the 0. letter)
 * Expressed in character index and not byte index (different in UTF-8)
 */
uint16_t lv_label_get_letter_on(lv_obj_t * label, point_t * pos)
{
	const char * txt = lv_label_get_text(label);
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    uint32_t line_start = 0;
    uint32_t new_line_start = 0;
    cord_t max_w = lv_obj_get_width(label);
    lv_style_t * style = lv_obj_get_style(label);
    const font_t * font = style->text.font;
    uint8_t letter_height = font_get_height(font) >> FONT_ANTIALIAS;
    cord_t y = 0;
    txt_flag_t flag = TXT_FLAG_NONE;

    if(ext->recolor != 0) flag |= TXT_FLAG_RECOLOR;
    if(ext->expand != 0) flag |= TXT_FLAG_EXPAND;
    if(ext->no_break != 0) flag |= TXT_FLAG_NO_BREAK;
    if(ext->align == LV_LABEL_ALIGN_CENTER) flag |= TXT_FLAG_CENTER;

    /*If the width will be expanded set the max length to very big */
    if(ext->long_mode == LV_LABEL_LONG_EXPAND || ext->long_mode == LV_LABEL_LONG_SCROLL) {
        max_w = CORD_MAX;
    }

    /*Search the line of the index letter */;
    while (txt[line_start] != '\0') {
    	new_line_start += txt_get_next_line(&txt[line_start], font, style->text.letter_space, max_w, flag);
    	if(pos->y <= y + letter_height) break; /*The line is found (stored in 'line_start')*/
    	y += letter_height + style->text.line_space;
        line_start = new_line_start;
    }

    /*Calculate the x coordinate*/
    cord_t x = 0;
	if(ext->align == LV_LABEL_ALIGN_CENTER) {
		cord_t line_w;
        line_w = txt_get_width(&txt[line_start], new_line_start - line_start,
                               font, style->text.letter_space, flag);
		x += lv_obj_get_width(label) / 2 - line_w / 2;
    }

	txt_cmd_state_t cmd_state = TXT_CMD_STATE_WAIT;
	uint32_t i = line_start;
    uint32_t i_current = i;
	uint32_t letter;
	while(i < new_line_start - 1) {
	    letter = txt_utf8_next(txt, &i);    /*Be careful 'i' already points to the next character*/
	    /*Handle the recolor command*/
	    if((flag & TXT_FLAG_RECOLOR) != 0) {
            if(txt_is_cmd(&cmd_state, txt[i]) != false) {
                continue; /*Skip the letter is it is part of a command*/
            }
	    }

	    x += (font_get_width(font, letter) >> FONT_ANTIALIAS);
		if(pos->x < x) {
		    i = i_current;
		    break;
		}
		x += style->text.letter_space;
		i_current = i;
	}

	return txt_utf8_get_char_id(txt, i);
}


/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Handle the drawing related tasks of the labels
 * @param label pointer to a label object
 * @param mask the object will be drawn only in this area
 * @param mode LV_DESIGN_COVER_CHK: only check if the object fully covers the 'mask_p' area
 *                                  (return 'true' if yes)
 *             LV_DESIGN_DRAW: draw the object (always return 'true')
 *             LV_DESIGN_DRAW_POST: drawing after every children are drawn
 * @param return true/false, depends on 'mode'
 */
static bool lv_label_design(lv_obj_t * label, const area_t * mask, lv_design_mode_t mode)
{
    /* A label never covers an area */
    if(mode == LV_DESIGN_COVER_CHK) return false;
    else if(mode == LV_DESIGN_DRAW_MAIN) {
        area_t cords;
        lv_style_t * style = lv_obj_get_style(label);
        lv_obj_get_coords(label, &cords);

#if LV_OBJ_GROUP != 0
        lv_group_t * g = lv_obj_get_group(label);
        if(lv_group_get_focused(g) == label) {
            lv_draw_rect(&cords, mask, style);
        }
#endif

        lv_label_ext_t * ext = lv_obj_get_ext_attr(label);

        if(ext->body_draw) lv_draw_rect(&cords, mask, style);

        /*TEST: draw a background for the label*/
//		lv_vfill(&label->coords, mask, COLOR_LIME, OPA_COVER);

		txt_flag_t flag = TXT_FLAG_NONE;
		if(ext->recolor != 0) flag |= TXT_FLAG_RECOLOR;
        if(ext->expand != 0) flag |= TXT_FLAG_EXPAND;
        if(ext->no_break != 0) flag |= TXT_FLAG_NO_BREAK;
        if(ext->align == LV_LABEL_ALIGN_CENTER) flag |= TXT_FLAG_CENTER;

		lv_draw_label(&cords, mask, style, ext->txt, flag, &ext->offset);
    }
    return true;
}

/**
 * Refresh the label with its text stored in its extended data
 * @param label pointer to a label object
 */
static void lv_label_refr_text(lv_obj_t * label)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);

    if(ext->txt == NULL) return;

    cord_t max_w = lv_obj_get_width(label);
    lv_style_t * style = lv_obj_get_style(label);
    const font_t * font = style->text.font;

    ext->dot_end = LV_LABEL_DOT_END_INV;    /*Initialize the dot end index*/

    /*If the width will be expanded set the max length to very big */
    if(ext->long_mode == LV_LABEL_LONG_EXPAND ||
       ext->long_mode == LV_LABEL_LONG_SCROLL) {
        max_w = CORD_MAX;
    }

    /*Calc. the height and longest line*/
    point_t size;
    txt_flag_t flag = TXT_FLAG_NONE;
    if(ext->recolor != 0) flag |= TXT_FLAG_RECOLOR;
    if(ext->expand != 0) flag |= TXT_FLAG_EXPAND;
    if(ext->no_break != 0) flag |= TXT_FLAG_NO_BREAK;
    txt_get_size(&size, ext->txt, font, style->text.letter_space, style->text.line_space, max_w, flag);

    /*Refresh the full size in expand mode*/
    if(ext->long_mode == LV_LABEL_LONG_EXPAND || ext->long_mode == LV_LABEL_LONG_SCROLL) {
        lv_obj_set_size(label, size.x, size.y);

        /*Start scrolling if the label is greater then its parent*/
        if(ext->long_mode == LV_LABEL_LONG_SCROLL) {
            lv_obj_t * parent = lv_obj_get_parent(label);

            /*Delete the potential previous scroller animations*/
            anim_del(label, (anim_fp_t) lv_obj_set_x);
            anim_del(label, (anim_fp_t) lv_obj_set_y);

            anim_t anim;
            anim.var = label;
            anim.repeat = 1;
            anim.playback = 1;
            anim.start = font_get_width(font, ' ') >> FONT_ANTIALIAS;
            anim.act_time = 0;
            anim.end_cb = NULL;
            anim.path = anim_get_path(ANIM_PATH_LIN);
            anim.time = 3000;
            anim.playback_pause = LV_LABEL_SCROLL_PLAYBACK_PAUSE;
            anim.repeat_pause = LV_LABEL_SCROLL_REPEAT_PAUSE;

            bool hor_anim = false;
            if(lv_obj_get_width(label) > lv_obj_get_width(parent)) {
                anim.end = lv_obj_get_width(parent) - lv_obj_get_width(label) -
                           (font_get_width(font, ' ') >> FONT_ANTIALIAS);
                anim.fp = (anim_fp_t) lv_obj_set_x;
                anim.time = anim_speed_to_time(LV_LABEL_SCROLL_SPEED, anim.start, anim.end);
                anim_create(&anim);
                hor_anim = true;
            }

            if(lv_obj_get_height(label) > lv_obj_get_height(parent)) {
                anim.end =  lv_obj_get_height(parent) - lv_obj_get_height(label) -
                                   (font_get_height(font) >> FONT_ANTIALIAS);
                anim.fp = (anim_fp_t)lv_obj_set_y;

                /*Different animation speed if horizontal animation is created too*/
                if(hor_anim == false) {
                    anim.time = anim_speed_to_time(LV_LABEL_SCROLL_SPEED, anim.start, anim.end);
                } else {
                    anim.time = anim_speed_to_time(LV_LABEL_SCROLL_SPEED_VER, anim.start, anim.end);
                }
                anim_create(&anim);
            }
        }
    }
    /*In roll mode keep the size but start offset animations*/
    else if(ext->long_mode == LV_LABEL_LONG_ROLL) {
        anim_t anim;
        anim.var = label;
        anim.repeat = 1;
        anim.playback = 1;
        anim.start = font_get_width(font, ' ') >> FONT_ANTIALIAS;
        anim.act_time = 0;
        anim.end_cb = NULL;
        anim.path = anim_get_path(ANIM_PATH_LIN);
        anim.playback_pause = LV_LABEL_SCROLL_PLAYBACK_PAUSE;
        anim.repeat_pause = LV_LABEL_SCROLL_REPEAT_PAUSE;

        bool hor_anim = false;
        if(size.x > lv_obj_get_width(label)) {
            anim.end = lv_obj_get_width(label) - size.x -
                       (font_get_width(font, ' ') >> FONT_ANTIALIAS);
            anim.fp = (anim_fp_t) lv_label_set_offset_x;
            anim.time = anim_speed_to_time(LV_LABEL_SCROLL_SPEED, anim.start, anim.end);
            anim_create(&anim);
            hor_anim = true;
        } else {
            /*Delete the offset animation if not required*/
            anim_del(label, (anim_fp_t) lv_label_set_offset_x);
            ext->offset.x = 0;
        }

        if(size.y > lv_obj_get_height(label)) {
            anim.end =  lv_obj_get_height(label) - size.y -
                               (font_get_height(font) >> FONT_ANTIALIAS);
            anim.fp = (anim_fp_t)lv_label_set_offset_y;

            /*Different animation speed if horizontal animation is created too*/
            if(hor_anim == false) {
                anim.time = anim_speed_to_time(LV_LABEL_SCROLL_SPEED, anim.start, anim.end);
            } else {
                anim.time = anim_speed_to_time(LV_LABEL_SCROLL_SPEED_VER, anim.start, anim.end);
            }
            anim_create(&anim);
        } else {
            /*Delete the offset animation if not required*/
            anim_del(label, (anim_fp_t) lv_label_set_offset_y);
            ext->offset.y = 0;

        }
    }
    /*In break mode only the height can change*/
    else if (ext->long_mode == LV_LABEL_LONG_BREAK) {
        lv_obj_set_height(label, size.y);
    }


    lv_obj_invalidate(label);
}


static void lv_label_set_offset_x(lv_obj_t * label, cord_t x)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    ext->offset.x = x;
    lv_obj_invalidate(label);
}

static void lv_label_set_offset_y(lv_obj_t * label, cord_t y)
{
    lv_label_ext_t * ext = lv_obj_get_ext_attr(label);
    ext->offset.y = y;
    lv_obj_invalidate(label);
}
#endif
