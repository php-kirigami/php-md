/*
  +----------------------------------------------------------------------+
  | php-mdhtml                                                            |
  +----------------------------------------------------------------------+
  | Copyright (c) Maxime Larrivée-Roy                                     |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2 of the GNU General Public   |
  | License, that is bundled with this package in the file LICENSE.      |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "ext/standard/info.h"
#include "zend_smart_str.h"
#include "php_mdhtml.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

#include <string.h>
#include <ctype.h>

/* ========================================================================
 * MODULE GLOBALS (type declared in php_mdhtml.h)
 *
 * `emoji_builtin` is populated once in MINIT and never touched again --
 * a straight C-array-to-HashTable copy of MD::$emojiMap (kirigami/php-prepros).
 * `emoji_custom` mirrors MD::$extraEmoji / MD::registerEmoji(): request-
 * scoped (RINIT/RSHUTDOWN), so registrations from one request never leak
 * into the next in a long-running SAPI. Looked up custom-first, exactly
 * like MD::emojiFor().
 * ========================================================================
 */
ZEND_DECLARE_MODULE_GLOBALS(mdhtml)

typedef struct {
	const char *name;
	const char *utf8;
} php_mdhtml_emoji_entry;

/* Ported verbatim from MD::$emojiMap (kirigami/packages/php-prepros/src/
 * libraries/md.class.php) so php-mdhtml reproduces the exact same
 * shortcodes. Kept in the same grouping/order as the source for easy
 * re-diffing. */
static const php_mdhtml_emoji_entry php_mdhtml_builtin_emoji[] = {
	{"smile", "\xF0\x9F\x98\x84"}, {"smiley", "\xF0\x9F\x98\x83"}, {"grin", "\xF0\x9F\x98\x81"},
	{"joy", "\xF0\x9F\x98\x82"}, {"rofl", "\xF0\x9F\xA4\xA3"},
	{"blush", "\xF0\x9F\x98\x8A"}, {"wink", "\xF0\x9F\x98\x89"}, {"relaxed", "\xE2\x98\xBA\xEF\xB8\x8F"},
	{"slight_smile", "\xF0\x9F\x99\x82"},
	{"upside_down_face", "\xF0\x9F\x99\x83"}, {"innocent", "\xF0\x9F\x98\x87"}, {"heart_eyes", "\xF0\x9F\x98\x8D"},
	{"kissing_heart", "\xF0\x9F\x98\x98"},
	{"thinking", "\xF0\x9F\xA4\x94"}, {"neutral_face", "\xF0\x9F\x98\x90"}, {"expressionless", "\xF0\x9F\x98\x91"},
	{"no_mouth", "\xF0\x9F\x98\xB6"},
	{"roll_eyes", "\xF0\x9F\x99\x84"}, {"smirk", "\xF0\x9F\x98\x8F"}, {"unamused", "\xF0\x9F\x98\x92"},
	{"grimacing", "\xF0\x9F\x98\xAC"},
	{"lying_face", "\xF0\x9F\xA4\xA5"}, {"relieved", "\xF0\x9F\x98\x8C"}, {"pensive", "\xF0\x9F\x98\x94"},
	{"sleepy", "\xF0\x9F\x98\xAA"},
	{"drooling_face", "\xF0\x9F\xA4\xA4"}, {"sleeping", "\xF0\x9F\x98\xB4"}, {"mask", "\xF0\x9F\x98\xB7"},
	{"sunglasses", "\xF0\x9F\x98\x8E"},
	{"star_struck", "\xF0\x9F\xA4\xA9"}, {"partying_face", "\xF0\x9F\xA5\xB3"}, {"worried", "\xF0\x9F\x98\x9F"},
	{"frowning", "\xE2\x98\xB9\xEF\xB8\x8F"},
	{"confused", "\xF0\x9F\x98\x95"}, {"slightly_frowning_face", "\xF0\x9F\x99\x81"}, {"cry", "\xF0\x9F\x98\xA2"},
	{"sob", "\xF0\x9F\x98\xAD"},
	{"scream", "\xF0\x9F\x98\xB1"}, {"confounded", "\xF0\x9F\x98\x96"}, {"persevere", "\xF0\x9F\x98\xA3"},
	{"disappointed", "\xF0\x9F\x98\x9E"},
	{"sweat", "\xF0\x9F\x98\x93"}, {"weary", "\xF0\x9F\x98\xA9"}, {"tired_face", "\xF0\x9F\x98\xAB"},
	{"yawning_face", "\xF0\x9F\xA5\xB1"},
	{"triumph", "\xF0\x9F\x98\xA4"}, {"rage", "\xF0\x9F\x98\xA1"}, {"angry", "\xF0\x9F\x98\xA0"},
	{"cursing_face", "\xF0\x9F\xA4\xAC"},
	{"exploding_head", "\xF0\x9F\xA4\xAF"}, {"flushed", "\xF0\x9F\x98\xB3"}, {"hot_face", "\xF0\x9F\xA5\xB5"},
	{"cold_face", "\xF0\x9F\xA5\xB6"},
	{"scream_cat", "\xF0\x9F\x99\x80"}, {"nerd_face", "\xF0\x9F\xA4\x93"}, {"monocle_face", "\xF0\x9F\xA7\x90"},
	{"zany_face", "\xF0\x9F\xA4\xAA"},
	{"raised_eyebrow", "\xF0\x9F\xA4\xA8"}, {"shushing_face", "\xF0\x9F\xA4\xAB"}, {"zipper_mouth_face", "\xF0\x9F\xA4\x90"},
	{"heart", "\xE2\x9D\xA4\xEF\xB8\x8F"}, {"orange_heart", "\xF0\x9F\xA7\xA1"}, {"yellow_heart", "\xF0\x9F\x92\x9B"},
	{"green_heart", "\xF0\x9F\x92\x9A"},
	{"blue_heart", "\xF0\x9F\x92\x99"}, {"purple_heart", "\xF0\x9F\x92\x9C"}, {"black_heart", "\xF0\x9F\x96\xA4"},
	{"white_heart", "\xF0\x9F\xA4\x8D"},
	{"broken_heart", "\xF0\x9F\x92\x94"}, {"two_hearts", "\xF0\x9F\x92\x95"}, {"sparkling_heart", "\xF0\x9F\x92\x96"},
	{"heartbeat", "\xF0\x9F\x92\x93"},
	{"thumbsup", "\xF0\x9F\x91\x8D"}, {"+1", "\xF0\x9F\x91\x8D"}, {"thumbsdown", "\xF0\x9F\x91\x8E"}, {"-1", "\xF0\x9F\x91\x8E"},
	{"clap", "\xF0\x9F\x91\x8F"}, {"raised_hands", "\xF0\x9F\x99\x8C"}, {"pray", "\xF0\x9F\x99\x8F"}, {"wave", "\xF0\x9F\x91\x8B"},
	{"ok_hand", "\xF0\x9F\x91\x8C"}, {"v", "\xE2\x9C\x8C\xEF\xB8\x8F"}, {"crossed_fingers", "\xF0\x9F\xA4\x9E"},
	{"muscle", "\xF0\x9F\x92\xAA"},
	{"point_up", "\xE2\x98\x9D\xEF\xB8\x8F"}, {"point_down", "\xF0\x9F\x91\x87"}, {"point_left", "\xF0\x9F\x91\x88"},
	{"point_right", "\xF0\x9F\x91\x89"},
	{"handshake", "\xF0\x9F\xA4\x9D"}, {"writing_hand", "\xE2\x9C\x8D\xEF\xB8\x8F"}, {"fire", "\xF0\x9F\x94\xA5"},
	{"star", "\xE2\xAD\x90"},
	{"star2", "\xF0\x9F\x8C\x9F"}, {"sparkles", "\xE2\x9C\xA8"}, {"zap", "\xE2\x9A\xA1"}, {"boom", "\xF0\x9F\x92\xA5"},
	{"collision", "\xF0\x9F\x92\xA5"},
	{"rocket", "\xF0\x9F\x9A\x80"}, {"tada", "\xF0\x9F\x8E\x89"}, {"confetti_ball", "\xF0\x9F\x8E\x8A"},
	{"gift", "\xF0\x9F\x8E\x81"},
	{"balloon", "\xF0\x9F\x8E\x88"}, {"trophy", "\xF0\x9F\x8F\x86"}, {"medal", "\xF0\x9F\x8F\x85"}, {"crown", "\xF0\x9F\x91\x91"},
	{"gem", "\xF0\x9F\x92\x8E"}, {"moneybag", "\xF0\x9F\x92\xB0"}, {"dollar", "\xF0\x9F\x92\xB5"}, {"100", "\xF0\x9F\x92\xAF"},
	{"warning", "\xE2\x9A\xA0\xEF\xB8\x8F"}, {"no_entry", "\xE2\x9B\x94"}, {"stop_sign", "\xF0\x9F\x9B\x91"},
	{"checkered_flag", "\xF0\x9F\x8F\x81"},
	{"white_check_mark", "\xE2\x9C\x85"}, {"heavy_check_mark", "\xE2\x9C\x94\xEF\xB8\x8F"}, {"x", "\xE2\x9D\x8C"},
	{"negative_squared_cross_mark", "\xE2\x9D\x8E"},
	{"question", "\xE2\x9D\x93"}, {"grey_question", "\xE2\x9D\x94"}, {"exclamation", "\xE2\x9D\x97"},
	{"bangbang", "\xE2\x80\xBC\xEF\xB8\x8F"},
	{"interrobang", "\xE2\x81\x89\xEF\xB8\x8F"}, {"bulb", "\xF0\x9F\x92\xA1"}, {"bell", "\xF0\x9F\x94\x94"},
	{"no_bell", "\xF0\x9F\x94\x95"},
	{"lock", "\xF0\x9F\x94\x92"}, {"unlock", "\xF0\x9F\x94\x93"}, {"key", "\xF0\x9F\x94\x91"}, {"mag", "\xF0\x9F\x94\x8D"},
	{"link", "\xF0\x9F\x94\x97"},
	{"pushpin", "\xF0\x9F\x93\x8C"}, {"paperclip", "\xF0\x9F\x93\x8E"}, {"calendar", "\xF0\x9F\x93\x85"},
	{"clock", "\xF0\x9F\x95\x90"},
	{"hourglass", "\xE2\x8C\x9B"}, {"alarm_clock", "\xE2\x8F\xB0"}, {"memo", "\xF0\x9F\x93\x9D"},
	{"pencil2", "\xE2\x9C\x8F\xEF\xB8\x8F"},
	{"book", "\xF0\x9F\x93\x96"}, {"books", "\xF0\x9F\x93\x9A"}, {"newspaper", "\xF0\x9F\x93\xB0"}, {"email", "\xF0\x9F\x93\xA7"},
	{"envelope", "\xE2\x9C\x89\xEF\xB8\x8F"}, {"inbox_tray", "\xF0\x9F\x93\xA5"}, {"outbox_tray", "\xF0\x9F\x93\xA4"},
	{"package", "\xF0\x9F\x93\xA6"},
	{"file_folder", "\xF0\x9F\x93\x81"}, {"open_file_folder", "\xF0\x9F\x93\x82"}, {"clipboard", "\xF0\x9F\x93\x8B"},
	{"chart_with_upwards_trend", "\xF0\x9F\x93\x88"}, {"chart_with_downwards_trend", "\xF0\x9F\x93\x89"},
	{"bar_chart", "\xF0\x9F\x93\x8A"},
	{"computer", "\xF0\x9F\x92\xBB"}, {"desktop_computer", "\xF0\x9F\x96\xA5\xEF\xB8\x8F"},
	{"keyboard", "\xE2\x8C\xA8\xEF\xB8\x8F"}, {"printer", "\xF0\x9F\x96\xA8\xEF\xB8\x8F"},
	{"phone", "\xF0\x9F\x93\xB1"}, {"iphone", "\xF0\x9F\x93\xB1"}, {"camera", "\xF0\x9F\x93\xB7"},
	{"video_camera", "\xF0\x9F\x93\xB9"},
	{"tv", "\xF0\x9F\x93\xBA"}, {"radio", "\xF0\x9F\x93\xBB"}, {"battery", "\xF0\x9F\x94\x8B"},
	{"electric_plug", "\xF0\x9F\x94\x8C"},
	{"bug", "\xF0\x9F\x90\x9B"}, {"beetle", "\xF0\x9F\xAA\xB2"}, {"gear", "\xE2\x9A\x99\xEF\xB8\x8F"},
	{"wrench", "\xF0\x9F\x94\xA7"}, {"hammer", "\xF0\x9F\x94\xA8"},
	{"nut_and_bolt", "\xF0\x9F\x94\xA9"}, {"toolbox", "\xF0\x9F\xA7\xB0"}, {"test_tube", "\xF0\x9F\xA7\xAA"},
	{"microscope", "\xF0\x9F\x94\xAC"},
	{"satellite", "\xF0\x9F\x9B\xB0\xEF\xB8\x8F"}, {"globe_with_meridians", "\xF0\x9F\x8C\x90"},
	{"earth_americas", "\xF0\x9F\x8C\x8E"},
	{"sun", "\xE2\x98\x80\xEF\xB8\x8F"}, {"sunny", "\xE2\x98\x80\xEF\xB8\x8F"}, {"partly_sunny", "\xE2\x9B\x85"},
	{"cloud", "\xE2\x98\x81\xEF\xB8\x8F"},
	{"rainbow", "\xF0\x9F\x8C\x88"}, {"umbrella", "\xE2\x98\x82\xEF\xB8\x8F"}, {"snowflake", "\xE2\x9D\x84\xEF\xB8\x8F"},
	{"droplet", "\xF0\x9F\x92\xA7"},
	{"ocean", "\xF0\x9F\x8C\x8A"}, {"tent", "\xE2\x9B\xBA"}, {"camping", "\xF0\x9F\x8F\x95\xEF\xB8\x8F"},
	{"mountain", "\xE2\x9B\xB0\xEF\xB8\x8F"},
	{"evergreen_tree", "\xF0\x9F\x8C\xB2"}, {"deciduous_tree", "\xF0\x9F\x8C\xB3"}, {"palm_tree", "\xF0\x9F\x8C\xB4"},
	{"cactus", "\xF0\x9F\x8C\xB5"}, {"seedling", "\xF0\x9F\x8C\xB1"}, {"four_leaf_clover", "\xF0\x9F\x8D\x80"},
	{"maple_leaf", "\xF0\x9F\x8D\x81"},
	{"dog", "\xF0\x9F\x90\xB6"}, {"cat", "\xF0\x9F\x90\xB1"}, {"mouse", "\xF0\x9F\x90\xAD"}, {"rabbit", "\xF0\x9F\x90\xB0"},
	{"fox_face", "\xF0\x9F\xA6\x8A"},
	{"bear", "\xF0\x9F\x90\xBB"}, {"panda_face", "\xF0\x9F\x90\xBC"}, {"koala", "\xF0\x9F\x90\xA8"},
	{"tiger", "\xF0\x9F\x90\xAF"}, {"lion", "\xF0\x9F\xA6\x81"},
	{"cow", "\xF0\x9F\x90\xAE"}, {"pig", "\xF0\x9F\x90\xB7"}, {"frog", "\xF0\x9F\x90\xB8"}, {"monkey_face", "\xF0\x9F\x90\xB5"},
	{"chicken", "\xF0\x9F\x90\x94"},
	{"penguin", "\xF0\x9F\x90\xA7"}, {"bird", "\xF0\x9F\x90\xA6"}, {"baby_chick", "\xF0\x9F\x90\xA4"}, {"owl", "\xF0\x9F\xA6\x89"},
	{"horse", "\xF0\x9F\x90\xB4"}, {"unicorn", "\xF0\x9F\xA6\x84"}, {"bee", "\xF0\x9F\x90\x9D"},
	{"butterfly", "\xF0\x9F\xA6\x8B"}, {"snail", "\xF0\x9F\x90\x8C"},
	{"octopus", "\xF0\x9F\x90\x99"}, {"fish", "\xF0\x9F\x90\x9F"}, {"dolphin", "\xF0\x9F\x90\xAC"}, {"whale", "\xF0\x9F\x90\xB3"},
	{"pizza", "\xF0\x9F\x8D\x95"}, {"hamburger", "\xF0\x9F\x8D\x94"}, {"fries", "\xF0\x9F\x8D\x9F"}, {"hotdog", "\xF0\x9F\x8C\xAD"},
	{"taco", "\xF0\x9F\x8C\xAE"}, {"sushi", "\xF0\x9F\x8D\xA3"}, {"ramen", "\xF0\x9F\x8D\x9C"}, {"spaghetti", "\xF0\x9F\x8D\x9D"},
	{"bread", "\xF0\x9F\x8D\x9E"}, {"cheese", "\xF0\x9F\xA7\x80"}, {"egg", "\xF0\x9F\xA5\x9A"}, {"popcorn", "\xF0\x9F\x8D\xBF"},
	{"cookie", "\xF0\x9F\x8D\xAA"}, {"doughnut", "\xF0\x9F\x8D\xA9"}, {"cake", "\xF0\x9F\x8D\xB0"}, {"birthday", "\xF0\x9F\x8E\x82"},
	{"candy", "\xF0\x9F\x8D\xAC"}, {"chocolate_bar", "\xF0\x9F\x8D\xAB"}, {"icecream", "\xF0\x9F\x8D\xA6"},
	{"apple", "\xF0\x9F\x8D\x8E"},
	{"banana", "\xF0\x9F\x8D\x8C"}, {"grapes", "\xF0\x9F\x8D\x87"}, {"watermelon", "\xF0\x9F\x8D\x89"},
	{"strawberry", "\xF0\x9F\x8D\x93"},
	{"lemon", "\xF0\x9F\x8D\x8B"}, {"peach", "\xF0\x9F\x8D\x91"}, {"coffee", "\xE2\x98\x95"}, {"tea", "\xF0\x9F\x8D\xB5"},
	{"beer", "\xF0\x9F\x8D\xBA"},
	{"beers", "\xF0\x9F\x8D\xBB"}, {"wine_glass", "\xF0\x9F\x8D\xB7"}, {"cocktail", "\xF0\x9F\x8D\xB8"},
	{"tropical_drink", "\xF0\x9F\x8D\xB9"},
	{"champagne", "\xF0\x9F\x8D\xBE"}, {"soccer", "\xE2\x9A\xBD"}, {"basketball", "\xF0\x9F\x8F\x80"},
	{"football", "\xF0\x9F\x8F\x88"},
	{"baseball", "\xE2\x9A\xBE"}, {"tennis", "\xF0\x9F\x8E\xBE"}, {"volleyball", "\xF0\x9F\x8F\x90"},
	{"rugby_football", "\xF0\x9F\x8F\x89"},
	{"8ball", "\xF0\x9F\x8E\xB1"}, {"golf", "\xE2\x9B\xB3"}, {"dart", "\xF0\x9F\x8E\xAF"}, {"video_game", "\xF0\x9F\x8E\xAE"},
	{"game_die", "\xF0\x9F\x8E\xB2"}, {"jigsaw", "\xF0\x9F\xA7\xA9"}, {"car", "\xF0\x9F\x9A\x97"}, {"taxi", "\xF0\x9F\x9A\x95"},
	{"bus", "\xF0\x9F\x9A\x8C"},
	{"ambulance", "\xF0\x9F\x9A\x91"}, {"fire_engine", "\xF0\x9F\x9A\x92"}, {"police_car", "\xF0\x9F\x9A\x93"},
	{"bike", "\xF0\x9F\x9A\xB2"},
	{"airplane", "\xE2\x9C\x88\xEF\xB8\x8F"}, {"helicopter", "\xF0\x9F\x9A\x81"}, {"train", "\xF0\x9F\x9A\x86"},
	{"ship", "\xF0\x9F\x9A\xA2"},
	{"house", "\xF0\x9F\x8F\xA0"}, {"office", "\xF0\x9F\x8F\xA2"}, {"hospital", "\xF0\x9F\x8F\xA5"},
	{"school", "\xF0\x9F\x8F\xAB"},
	{"church", "\xE2\x9B\xAA"}, {"castle", "\xF0\x9F\x8F\xB0"}, {"world_map", "\xF0\x9F\x97\xBA\xEF\xB8\x8F"},
	{"flag_white", "\xF0\x9F\x8F\xB3\xEF\xB8\x8F"},
	{"flag_black", "\xF0\x9F\x8F\xB4"}, {"checkered_flag2", "\xF0\x9F\x8F\x81"}, {"eyes", "\xF0\x9F\x91\x80"},
	{"eye", "\xF0\x9F\x91\x81\xEF\xB8\x8F"},
	{"speech_balloon", "\xF0\x9F\x92\xAC"}, {"thought_balloon", "\xF0\x9F\x92\xAD"}, {"zzz", "\xF0\x9F\x92\xA4"},
	{"boom2", "\xF0\x9F\x92\xA5"},
	{"sos", "\xF0\x9F\x86\x98"}, {"new", "\xF0\x9F\x86\x95"}, {"ok", "\xF0\x9F\x86\x97"}, {"up", "\xF0\x9F\x86\x99"},
	{"cool", "\xF0\x9F\x86\x92"},
	{"free", "\xF0\x9F\x86\x93"}, {"id", "\xF0\x9F\x86\x94"}, {"ng", "\xF0\x9F\x86\x96"},

	{"smiling_face_with_three_hearts", "\xF0\x9F\xA5\xB0"}, {"kissing", "\xF0\x9F\x98\x97"},
	{"kissing_closed_eyes", "\xF0\x9F\x98\x9A"},
	{"kissing_smiling_eyes", "\xF0\x9F\x98\x99"}, {"yum", "\xF0\x9F\x98\x8B"}, {"stuck_out_tongue", "\xF0\x9F\x98\x9B"},
	{"stuck_out_tongue_winking_eye", "\xF0\x9F\x98\x9C"}, {"stuck_out_tongue_closed_eyes", "\xF0\x9F\x98\x9D"},
	{"money_mouth_face", "\xF0\x9F\xA4\x91"}, {"hugs", "\xF0\x9F\xA4\x97"}, {"disappointed_relieved", "\xF0\x9F\x98\xA5"},
	{"dizzy_face", "\xF0\x9F\x98\xB5"}, {"astonished", "\xF0\x9F\x98\xB2"}, {"open_mouth", "\xF0\x9F\x98\xAE"},
	{"hushed", "\xF0\x9F\x98\xAF"},
	{"fearful", "\xF0\x9F\x98\xA8"}, {"cold_sweat", "\xF0\x9F\x98\xB0"}, {"nauseated_face", "\xF0\x9F\xA4\xA2"},
	{"vomiting_face", "\xF0\x9F\xA4\xAE"},
	{"sneezing_face", "\xF0\x9F\xA4\xA7"}, {"face_with_thermometer", "\xF0\x9F\xA4\x92"},
	{"face_with_head_bandage", "\xF0\x9F\xA4\x95"},
	{"woozy_face", "\xF0\x9F\xA5\xB4"}, {"smiling_imp", "\xF0\x9F\x98\x88"}, {"imp", "\xF0\x9F\x91\xBF"},
	{"japanese_ogre", "\xF0\x9F\x91\xB9"},
	{"japanese_goblin", "\xF0\x9F\x91\xBA"}, {"skull", "\xF0\x9F\x92\x80"}, {"skull_and_crossbones", "\xE2\x98\xA0\xEF\xB8\x8F"},
	{"ghost", "\xF0\x9F\x91\xBB"}, {"alien", "\xF0\x9F\x91\xBD"}, {"space_invader", "\xF0\x9F\x91\xBE"},
	{"robot", "\xF0\x9F\xA4\x96"},
	{"poop", "\xF0\x9F\x92\xA9"}, {"clown_face", "\xF0\x9F\xA4\xA1"}, {"smiley_cat", "\xF0\x9F\x98\xBA"},
	{"smile_cat", "\xF0\x9F\x98\xB8"},
	{"joy_cat", "\xF0\x9F\x98\xB9"}, {"heart_eyes_cat", "\xF0\x9F\x98\xBB"}, {"smirk_cat", "\xF0\x9F\x98\xBC"},
	{"kissing_cat", "\xF0\x9F\x98\xBD"},
	{"pouting_cat", "\xF0\x9F\x98\xBE"}, {"crying_cat_face", "\xF0\x9F\x98\xBF"},

	{"raised_hand", "\xE2\x9C\x8B"}, {"raised_back_of_hand", "\xF0\x9F\xA4\x9A"}, {"vulcan_salute", "\xF0\x9F\x96\x96"},
	{"pinching_hand", "\xF0\x9F\xA4\x8F"}, {"fist", "\xE2\x9C\x8A"}, {"punch", "\xF0\x9F\x91\x8A"},
	{"left_facing_fist", "\xF0\x9F\xA4\x9B"},
	{"right_facing_fist", "\xF0\x9F\xA4\x9C"}, {"open_hands", "\xF0\x9F\x91\x90"}, {"palms_up_together", "\xF0\x9F\xA4\xB2"},
	{"nail_care", "\xF0\x9F\x92\x85"}, {"selfie", "\xF0\x9F\xA4\xB3"}, {"ear", "\xF0\x9F\x91\x82"}, {"nose", "\xF0\x9F\x91\x83"},
	{"brain", "\xF0\x9F\xA7\xA0"},
	{"tongue", "\xF0\x9F\x91\x85"}, {"lips", "\xF0\x9F\x91\x84"}, {"tooth", "\xF0\x9F\xA6\xB7"}, {"bone", "\xF0\x9F\xA6\xB4"},
	{"baby", "\xF0\x9F\x91\xB6"}, {"child", "\xF0\x9F\xA7\x92"}, {"boy", "\xF0\x9F\x91\xA6"}, {"girl", "\xF0\x9F\x91\xA7"},
	{"adult", "\xF0\x9F\xA7\x91"},
	{"man", "\xF0\x9F\x91\xA8"}, {"woman", "\xF0\x9F\x91\xA9"}, {"older_adult", "\xF0\x9F\xA7\x93"},
	{"older_man", "\xF0\x9F\x91\xB4"}, {"older_woman", "\xF0\x9F\x91\xB5"},
	{"mage", "\xF0\x9F\xA7\x99"}, {"superhero", "\xF0\x9F\xA6\xB8"}, {"supervillain", "\xF0\x9F\xA6\xB9"},
	{"vampire", "\xF0\x9F\xA7\x9B"},
	{"zombie", "\xF0\x9F\xA7\x9F"}, {"genie", "\xF0\x9F\xA7\x9E"}, {"merperson", "\xF0\x9F\xA7\x9C"}, {"elf", "\xF0\x9F\xA7\x9D"},
	{"fairy", "\xF0\x9F\xA7\x9A"},

	{"wolf", "\xF0\x9F\x90\xBA"}, {"boar", "\xF0\x9F\x90\x97"}, {"racehorse", "\xF0\x9F\x90\x8E"},
	{"zebra", "\xF0\x9F\xA6\x93"}, {"deer", "\xF0\x9F\xA6\x8C"},
	{"cow2", "\xF0\x9F\x90\x84"}, {"ox", "\xF0\x9F\x90\x82"}, {"water_buffalo", "\xF0\x9F\x90\x83"},
	{"pig2", "\xF0\x9F\x90\x96"}, {"ram", "\xF0\x9F\x90\x8F"},
	{"sheep", "\xF0\x9F\x90\x91"}, {"goat", "\xF0\x9F\x90\x90"}, {"camel", "\xF0\x9F\x90\xAB"}, {"dromedary_camel", "\xF0\x9F\x90\xAA"},
	{"llama", "\xF0\x9F\xA6\x99"}, {"giraffe", "\xF0\x9F\xA6\x92"}, {"elephant", "\xF0\x9F\x90\x98"},
	{"rhinoceros", "\xF0\x9F\xA6\x8F"},
	{"hippopotamus", "\xF0\x9F\xA6\x9B"}, {"mouse2", "\xF0\x9F\x90\x81"}, {"rat", "\xF0\x9F\x90\x80"},
	{"hamster", "\xF0\x9F\x90\xB9"},
	{"chipmunk", "\xF0\x9F\x90\xBF\xEF\xB8\x8F"}, {"hedgehog", "\xF0\x9F\xA6\x94"}, {"bat", "\xF0\x9F\xA6\x87"},
	{"duck", "\xF0\x9F\xA6\x86"}, {"eagle", "\xF0\x9F\xA6\x85"},
	{"flamingo", "\xF0\x9F\xA6\xA9"}, {"peacock", "\xF0\x9F\xA6\x9A"}, {"parrot", "\xF0\x9F\xA6\x9C"},
	{"swan", "\xF0\x9F\xA6\xA2"},
	{"turkey", "\xF0\x9F\xA6\x83"}, {"dove", "\xF0\x9F\x95\x8A\xEF\xB8\x8F"}, {"rooster", "\xF0\x9F\x90\x93"},
	{"crocodile", "\xF0\x9F\x90\x8A"},
	{"turtle", "\xF0\x9F\x90\xA2"}, {"lizard", "\xF0\x9F\xA6\x8E"}, {"snake", "\xF0\x9F\x90\x8D"},
	{"dragon_face", "\xF0\x9F\x90\xB2"},
	{"dragon", "\xF0\x9F\x90\x89"}, {"sauropod", "\xF0\x9F\xA6\x95"}, {"t-rex", "\xF0\x9F\xA6\x96"},
	{"whale2", "\xF0\x9F\x90\x8B"},
	{"shark", "\xF0\x9F\xA6\x88"}, {"seal", "\xF0\x9F\xA6\xAD"}, {"squid", "\xF0\x9F\xA6\x91"}, {"shrimp", "\xF0\x9F\xA6\x90"},
	{"lobster", "\xF0\x9F\xA6\x9E"},
	{"crab", "\xF0\x9F\xA6\x80"}, {"blowfish", "\xF0\x9F\x90\xA1"}, {"tropical_fish", "\xF0\x9F\x90\xA0"},
	{"oyster", "\xF0\x9F\xA6\xAA"},
	{"ant", "\xF0\x9F\x90\x9C"}, {"spider", "\xF0\x9F\x95\xB7\xEF\xB8\x8F"}, {"spider_web", "\xF0\x9F\x95\xB8\xEF\xB8\x8F"},
	{"scorpion", "\xF0\x9F\xA6\x82"},
	{"mosquito", "\xF0\x9F\xA6\x9F"}, {"microbe", "\xF0\x9F\xA6\xA0"}, {"paw_prints", "\xF0\x9F\x90\xBE"},

	{"cherry_blossom", "\xF0\x9F\x8C\xB8"}, {"blossom", "\xF0\x9F\x8C\xBC"}, {"rose", "\xF0\x9F\x8C\xB9"},
	{"wilted_flower", "\xF0\x9F\xA5\x80"},
	{"hibiscus", "\xF0\x9F\x8C\xBA"}, {"sunflower", "\xF0\x9F\x8C\xBB"}, {"tulip", "\xF0\x9F\x8C\xB7"},
	{"herb", "\xF0\x9F\x8C\xBF"},
	{"shamrock", "\xE2\x98\x98\xEF\xB8\x8F"}, {"fallen_leaf", "\xF0\x9F\x8D\x82"}, {"leaves", "\xF0\x9F\x8D\x83"},
	{"mushroom", "\xF0\x9F\x8D\x84"},
	{"chestnut", "\xF0\x9F\x8C\xB0"}, {"crescent_moon", "\xF0\x9F\x8C\x99"}, {"full_moon", "\xF0\x9F\x8C\x95"},
	{"new_moon", "\xF0\x9F\x8C\x91"},
	{"milky_way", "\xF0\x9F\x8C\x8C"}, {"stars", "\xF0\x9F\x8C\xA0"}, {"cyclone", "\xF0\x9F\x8C\x80"},
	{"fog", "\xF0\x9F\x8C\xAB"},
	{"wind_face", "\xF0\x9F\x8C\xAC"}, {"tornado", "\xF0\x9F\x8C\xAA"}, {"thunder_cloud_and_rain", "\xE2\x9B\x88\xEF\xB8\x8F"},
	{"sweat_drops", "\xF0\x9F\x92\xA6"}, {"snowman", "\xE2\x9B\x84"}, {"snowman_with_snow", "\xE2\x98\x83\xEF\xB8\x8F"},
	{"comet", "\xE2\x98\x84\xEF\xB8\x8F"},

	{"tomato", "\xF0\x9F\x8D\x85"}, {"eggplant", "\xF0\x9F\x8D\x86"}, {"avocado", "\xF0\x9F\xA5\x91"},
	{"broccoli", "\xF0\x9F\xA5\xA6"},
	{"carrot", "\xF0\x9F\xA5\x95"}, {"corn", "\xF0\x9F\x8C\xBD"}, {"hot_pepper", "\xF0\x9F\x8C\xB6\xEF\xB8\x8F"},
	{"cucumber", "\xF0\x9F\xA5\x92"},
	{"potato", "\xF0\x9F\xA5\x94"}, {"sweet_potato", "\xF0\x9F\x8D\xA0"}, {"peanuts", "\xF0\x9F\xA5\x9C"},
	{"honey_pot", "\xF0\x9F\x8D\xAF"},
	{"croissant", "\xF0\x9F\xA5\x90"}, {"bagel", "\xF0\x9F\xA5\xAF"}, {"pretzel", "\xF0\x9F\xA5\xA8"},
	{"pancakes", "\xF0\x9F\xA5\x9E"},
	{"waffle", "\xF0\x9F\xA7\x87"}, {"meat_on_bone", "\xF0\x9F\x8D\x96"}, {"poultry_leg", "\xF0\x9F\x8D\x97"},
	{"bacon", "\xF0\x9F\xA5\x93"},
	{"sandwich", "\xF0\x9F\xA5\xAA"}, {"stuffed_flatbread", "\xF0\x9F\xA5\x99"}, {"burrito", "\xF0\x9F\x8C\xAF"},
	{"salad", "\xF0\x9F\xA5\x97"},
	{"shallow_pan_of_food", "\xF0\x9F\xA5\x98"}, {"canned_food", "\xF0\x9F\xA5\xAB"}, {"bento", "\xF0\x9F\x8D\xB1"},
	{"rice_ball", "\xF0\x9F\x8D\x99"}, {"rice", "\xF0\x9F\x8D\x9A"}, {"curry", "\xF0\x9F\x8D\x9B"}, {"stew", "\xF0\x9F\x8D\xB2"},
	{"oden", "\xF0\x9F\x8D\xA2"},
	{"dango", "\xF0\x9F\x8D\xA1"}, {"shaved_ice", "\xF0\x9F\x8D\xA7"}, {"ice_cream", "\xF0\x9F\x8D\xA8"},
	{"pie", "\xF0\x9F\xA5\xA7"},
	{"cupcake", "\xF0\x9F\xA7\x81"}, {"moon_cake", "\xF0\x9F\xA5\xAE"}, {"lollipop", "\xF0\x9F\x8D\xAD"},
	{"custard", "\xF0\x9F\x8D\xAE"},
	{"milk_glass", "\xF0\x9F\xA5\x9B"}, {"baby_bottle", "\xF0\x9F\x8D\xBC"}, {"mate", "\xF0\x9F\xA7\x89"},
	{"ice_cube", "\xF0\x9F\xA7\x8A"},
	{"tumbler_glass", "\xF0\x9F\xA5\x83"}, {"cup_with_straw", "\xF0\x9F\xA5\xA4"}, {"chopsticks", "\xF0\x9F\xA5\xA2"},
	{"fork_and_knife", "\xF0\x9F\x8D\xB4"}, {"spoon", "\xF0\x9F\xA5\x84"}, {"plate_with_cutlery", "\xF0\x9F\x8D\xBD\xEF\xB8\x8F"},

	{"running", "\xF0\x9F\x8F\x83"}, {"walking", "\xF0\x9F\x9A\xB6"}, {"swimming", "\xF0\x9F\x8F\x8A"},
	{"surfing", "\xF0\x9F\x8F\x84"},
	{"skateboard", "\xF0\x9F\x9B\xB9"}, {"snowboarder", "\xF0\x9F\x8F\x82"}, {"weight_lifting", "\xF0\x9F\x8F\x8B\xEF\xB8\x8F"},
	{"cyclist", "\xF0\x9F\x9A\xB4"}, {"medal_military", "\xF0\x9F\x8E\x96\xEF\xB8\x8F"}, {"ticket", "\xF0\x9F\x8E\xAB"},
	{"circus_tent", "\xF0\x9F\x8E\xAA"},
	{"performing_arts", "\xF0\x9F\x8E\xAD"}, {"art", "\xF0\x9F\x8E\xA8"}, {"clapper", "\xF0\x9F\x8E\xAC"},
	{"microphone", "\xF0\x9F\x8E\xA4"},
	{"headphones", "\xF0\x9F\x8E\xA7"}, {"musical_note", "\xF0\x9F\x8E\xB5"}, {"musical_score", "\xF0\x9F\x8E\xBC"},
	{"guitar", "\xF0\x9F\x8E\xB8"},
	{"violin", "\xF0\x9F\x8E\xBB"}, {"drum", "\xF0\x9F\xA5\x81"}, {"trumpet", "\xF0\x9F\x8E\xBA"},
	{"saxophone", "\xF0\x9F\x8E\xB7"},
	{"musical_keyboard", "\xF0\x9F\x8E\xB9"}, {"chess_pawn", "\xE2\x99\x9F\xEF\xB8\x8F"}, {"bowling", "\xF0\x9F\x8E\xB3"},
	{"ice_skate", "\xE2\x9B\xB8\xEF\xB8\x8F"}, {"ski", "\xF0\x9F\x8E\xBF"}, {"fishing_pole_and_fish", "\xF0\x9F\x8E\xA3"},
	{"boxing_glove", "\xF0\x9F\xA5\x8A"}, {"martial_arts_uniform", "\xF0\x9F\xA5\x8B"}, {"goal_net", "\xF0\x9F\xA5\x85"},
	{"flying_disc", "\xF0\x9F\xA5\x8F"}, {"yo_yo", "\xF0\x9F\xAA\x80"}, {"kite", "\xF0\x9F\xAA\x81"},

	{"airplane_departure", "\xF0\x9F\x9B\xAB"}, {"airplane_arriving", "\xF0\x9F\x9B\xAC"},
	{"flying_saucer", "\xF0\x9F\x9B\xB8"},
	{"motorcycle", "\xF0\x9F\x8F\x8D\xEF\xB8\x8F"}, {"scooter", "\xF0\x9F\x9B\xB4"}, {"tractor", "\xF0\x9F\x9A\x9C"},
	{"truck", "\xF0\x9F\x9A\x9A"},
	{"articulated_lorry", "\xF0\x9F\x9A\x9B"}, {"trolleybus", "\xF0\x9F\x9A\x8E"}, {"minibus", "\xF0\x9F\x9A\x90"},
	{"metro", "\xF0\x9F\x9A\x87"},
	{"station", "\xF0\x9F\x9A\x89"}, {"monorail", "\xF0\x9F\x9A\x9D"}, {"bullettrain_front", "\xF0\x9F\x9A\x84"},
	{"steam_locomotive", "\xF0\x9F\x9A\x82"}, {"anchor", "\xE2\x9A\x93"}, {"sailboat", "\xE2\x9B\xB5"},
	{"canoe", "\xF0\x9F\x9B\xB6"},
	{"speedboat", "\xF0\x9F\x9A\xA4"}, {"ferry", "\xE2\x9B\xB4\xEF\xB8\x8F"}, {"passport_control", "\xF0\x9F\x9B\x82"},
	{"customs", "\xF0\x9F\x9B\x83"},
	{"baggage_claim", "\xF0\x9F\x9B\x84"}, {"left_luggage", "\xF0\x9F\x9B\x85"},
	{"vertical_traffic_light", "\xF0\x9F\x9A\xA6"},
	{"construction", "\xF0\x9F\x9A\xA7"}, {"fuelpump", "\xE2\x9B\xBD"}, {"busstop", "\xF0\x9F\x9A\x8F"},
	{"moyai", "\xF0\x9F\x97\xBF"},
	{"statue_of_liberty", "\xF0\x9F\x97\xBD"}, {"tokyo_tower", "\xF0\x9F\x97\xBC"}, {"fountain", "\xE2\x9B\xB2"},
	{"stadium", "\xF0\x9F\x8F\x9F\xEF\xB8\x8F"}, {"ferris_wheel", "\xF0\x9F\x8E\xA1"}, {"roller_coaster", "\xF0\x9F\x8E\xA2"},
	{"carousel_horse", "\xF0\x9F\x8E\xA0"}, {"beach_umbrella", "\xF0\x9F\x8F\x96\xEF\xB8\x8F"},
	{"desert", "\xF0\x9F\x8F\x9C\xEF\xB8\x8F"},
	{"desert_island", "\xF0\x9F\x8F\x9D\xEF\xB8\x8F"}, {"national_park", "\xF0\x9F\x8F\x9E\xEF\xB8\x8F"},
	{"sunrise", "\xF0\x9F\x8C\x85"},
	{"sunrise_over_mountains", "\xF0\x9F\x8C\x84"}, {"sparkler", "\xF0\x9F\x8E\x87"}, {"fireworks", "\xF0\x9F\x8E\x86"},
	{"city_sunset", "\xF0\x9F\x8C\x87"}, {"bridge_at_night", "\xF0\x9F\x8C\x89"}, {"houses", "\xF0\x9F\x8F\x98\xEF\xB8\x8F"},
	{"derelict_house", "\xF0\x9F\x8F\x9A\xEF\xB8\x8F"}, {"classical_building", "\xF0\x9F\x8F\x9B\xEF\xB8\x8F"},
	{"department_store", "\xF0\x9F\x8F\xAC"},
	{"post_office", "\xF0\x9F\x8F\xA3"}, {"hotel", "\xF0\x9F\x8F\xA8"}, {"convenience_store", "\xF0\x9F\x8F\xAA"},
	{"bank", "\xF0\x9F\x8F\xA6"},
	{"factory", "\xF0\x9F\x8F\xAD"},

	{"watch", "\xE2\x8C\x9A"}, {"stopwatch", "\xE2\x8F\xB1\xEF\xB8\x8F"}, {"timer_clock", "\xE2\x8F\xB2\xEF\xB8\x8F"},
	{"joystick", "\xF0\x9F\x95\xB9\xEF\xB8\x8F"},
	{"floppy_disk", "\xF0\x9F\x92\xBE"}, {"cd", "\xF0\x9F\x92\xBF"}, {"dvd", "\xF0\x9F\x93\x80"},
	{"movie_camera", "\xF0\x9F\x8E\xA5"},
	{"projector", "\xF0\x9F\x93\xBD\xEF\xB8\x8F"}, {"telephone", "\xE2\x98\x8E\xEF\xB8\x8F"}, {"pager", "\xF0\x9F\x93\x9F"},
	{"fax", "\xF0\x9F\x93\xA0"},
	{"candle", "\xF0\x9F\x95\xAF\xEF\xB8\x8F"}, {"fire_extinguisher", "\xF0\x9F\xA7\xAF"}, {"oil_drum", "\xF0\x9F\x9B\xA2\xEF\xB8\x8F"},
	{"money_with_wings", "\xF0\x9F\x92\xB8"}, {"credit_card", "\xF0\x9F\x92\xB3"}, {"yen", "\xF0\x9F\x92\xB4"},
	{"euro", "\xF0\x9F\x92\xB6"},
	{"pound", "\xF0\x9F\x92\xB7"}, {"briefcase", "\xF0\x9F\x92\xBC"}, {"balance_scale", "\xE2\x9A\x96\xEF\xB8\x8F"},
	{"compass", "\xF0\x9F\xA7\xAD"},
	{"triangular_ruler", "\xF0\x9F\x93\x90"}, {"straight_ruler", "\xF0\x9F\x93\x8F"}, {"round_pushpin", "\xF0\x9F\x93\x8D"},
	{"scissors", "\xE2\x9C\x82\xEF\xB8\x8F"}, {"thread", "\xF0\x9F\xA7\xB5"}, {"yarn", "\xF0\x9F\xA7\xB6"},
	{"safety_pin", "\xF0\x9F\xA7\xB7"},
	{"basket", "\xF0\x9F\xA7\xBA"}, {"hourglass_flowing_sand", "\xE2\x8F\xB3"}, {"notebook", "\xF0\x9F\x93\x93"},
	{"notebook_with_decorative_cover", "\xF0\x9F\x93\x94"}, {"page_facing_up", "\xF0\x9F\x93\x84"},
	{"page_with_curl", "\xF0\x9F\x93\x83"}, {"bookmark_tabs", "\xF0\x9F\x93\x91"}, {"bookmark", "\xF0\x9F\x94\x96"},
	{"label", "\xF0\x9F\x8F\xB7\xEF\xB8\x8F"}, {"receipt", "\xF0\x9F\xA7\xBE"}, {"card_index", "\xF0\x9F\x93\x87"},
	{"wastebasket", "\xF0\x9F\x97\x91\xEF\xB8\x8F"},
	{"old_key", "\xF0\x9F\x97\x9D\xEF\xB8\x8F"}, {"hammer_and_wrench", "\xF0\x9F\x9B\xA0\xEF\xB8\x8F"},
	{"pick", "\xE2\x9B\x8F\xEF\xB8\x8F"}, {"shield", "\xF0\x9F\x9B\xA1\xEF\xB8\x8F"},
	{"syringe", "\xF0\x9F\x92\x89"}, {"pill", "\xF0\x9F\x92\x8A"}, {"thermometer", "\xF0\x9F\x8C\xA1\xEF\xB8\x8F"},
	{"soap", "\xF0\x9F\xA7\xBC"},
	{"broom", "\xF0\x9F\xA7\xB9"},

	{"heavy_multiplication_x", "\xE2\x9C\x96\xEF\xB8\x8F"}, {"heavy_plus_sign", "\xE2\x9E\x95"},
	{"heavy_minus_sign", "\xE2\x9E\x96"},
	{"heavy_division_sign", "\xE2\x9E\x97"}, {"infinity", "\xE2\x99\xBE\xEF\xB8\x8F"}, {"recycle", "\xE2\x99\xBB\xEF\xB8\x8F"},
	{"trident", "\xF0\x9F\x94\xB1"},
	{"atom_symbol", "\xE2\x9A\x9B\xEF\xB8\x8F"}, {"om", "\xF0\x9F\x95\x89\xEF\xB8\x8F"}, {"peace_symbol", "\xE2\x98\xAE\xEF\xB8\x8F"},
	{"yin_yang", "\xE2\x98\xAF\xEF\xB8\x8F"},
	{"wheel_of_dharma", "\xE2\x98\xB8\xEF\xB8\x8F"}, {"star_of_david", "\xE2\x9C\xA1\xEF\xB8\x8F"},
	{"star_and_crescent", "\xE2\x98\xAA\xEF\xB8\x8F"},
	{"cross", "\xE2\x9C\x9D\xEF\xB8\x8F"}, {"menorah", "\xF0\x9F\x95\x8E"}, {"radioactive", "\xE2\x98\xA2\xEF\xB8\x8F"},
	{"biohazard", "\xE2\x98\xA3\xEF\xB8\x8F"},
	{"arrow_up", "\xE2\xAC\x86\xEF\xB8\x8F"}, {"arrow_down", "\xE2\xAC\x87\xEF\xB8\x8F"}, {"arrow_left", "\xE2\xAC\x85\xEF\xB8\x8F"},
	{"arrow_right", "\xE2\x9E\xA1\xEF\xB8\x8F"},
	{"arrow_upper_right", "\xE2\x86\x97\xEF\xB8\x8F"}, {"arrow_lower_right", "\xE2\x86\x98\xEF\xB8\x8F"},
	{"arrow_lower_left", "\xE2\x86\x99\xEF\xB8\x8F"},
	{"arrow_upper_left", "\xE2\x86\x96\xEF\xB8\x8F"}, {"arrows_clockwise", "\xF0\x9F\x94\x83"},
	{"arrows_counterclockwise", "\xF0\x9F\x94\x84"},
	{"back", "\xF0\x9F\x94\x99"}, {"end", "\xF0\x9F\x94\x9A"}, {"on", "\xF0\x9F\x94\x9B"}, {"soon", "\xF0\x9F\x94\x9C"},
	{"top", "\xF0\x9F\x94\x9D"},
	{"radio_button", "\xF0\x9F\x94\x98"}, {"red_circle", "\xF0\x9F\x94\xB4"}, {"orange_circle", "\xF0\x9F\x9F\xA0"},
	{"yellow_circle", "\xF0\x9F\x9F\xA1"}, {"green_circle", "\xF0\x9F\x9F\xA2"}, {"blue_circle", "\xF0\x9F\x94\xB5"},
	{"purple_circle", "\xF0\x9F\x9F\xA3"}, {"brown_circle", "\xF0\x9F\x9F\xA4"}, {"white_circle", "\xE2\x9A\xAA"},
	{"black_circle", "\xE2\x9A\xAB"},

	{"triangular_flag_on_post", "\xF0\x9F\x9A\xA9"}, {"crossed_flags", "\xF0\x9F\x8E\x8C"},
	{"us", "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"}, {"gb", "\xF0\x9F\x87\xAC\xF0\x9F\x87\xA7"},
	{"fr", "\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7"}, {"de", "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA"},
	{"es", "\xF0\x9F\x87\xAA\xF0\x9F\x87\xB8"},
	{"it", "\xF0\x9F\x87\xAE\xF0\x9F\x87\xB9"}, {"jp", "\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5"},
	{"cn", "\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3"}, {"kr", "\xF0\x9F\x87\xB0\xF0\x9F\x87\xB7"}, {"ca", "\xF0\x9F\x87\xA8\xF0\x9F\x87\xA6"},
	{"au", "\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA"}, {"br", "\xF0\x9F\x87\xA7\xF0\x9F\x87\xB7"}, {"in", "\xF0\x9F\x87\xAE\xF0\x9F\x87\xB3"},
	{"ru", "\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA"}, {"eu", "\xF0\x9F\x87\xAA\xF0\x9F\x87\xBA"},
};
#define PHP_MDHTML_BUILTIN_EMOJI_COUNT (sizeof(php_mdhtml_builtin_emoji) / sizeof(php_mdhtml_builtin_emoji[0]))

/* ========================================================================
 * HELPERS
 * ======================================================================== */

/* ASCII-only \w equivalent (letters/digits/underscore) -- MD::slugify()
 * uses PCRE's Unicode \w; replicating that fully in C without depending
 * on PHP's own PCRE internals is out of scope for now, so non-ASCII
 * heading text produces an ASCII-only slug here. Documented limitation,
 * revisit if a real Kirigami site needs non-ASCII heading anchors. */
static int php_mdhtml_is_word_char(unsigned char c) {
	return isalnum(c) || c == '_';
}

static zend_string *php_mdhtml_slugify(const char *text, size_t len) {
	smart_str out = {0};
	size_t i;
	int last_was_space = 0;
	int started = 0;

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char) text[i];
		if (php_mdhtml_is_word_char(c) || c == '-') {
			smart_str_appendc(&out, (char) tolower(c));
			last_was_space = 0;
			started = 1;
		} else if (c == ' ' || c == '\t') {
			if (started && !last_was_space) {
				smart_str_appendc(&out, '-');
				last_was_space = 1;
			}
		}
		/* everything else (matching MD::slugify's stripped charset) is
		 * silently dropped, same as its preg_replace('/[^\w\- ]/u','',...) */
	}
	/* trim a trailing '-' the same way trim()+space-to-dash in MD:: would
	 * not otherwise produce (a trailing space becomes a trailing dash
	 * above; MD:: trims the text BEFORE turning spaces into dashes) */
	while (out.s && ZSTR_LEN(out.s) > 0 && ZSTR_VAL(out.s)[ZSTR_LEN(out.s) - 1] == '-') {
		ZSTR_LEN(out.s)--;
	}
	if (!out.s) {
		return ZSTR_EMPTY_ALLOC();
	}
	smart_str_0(&out);
	return out.s;
}

static void php_mdhtml_collect_text(cmark_node *node, smart_str *out) {
	cmark_node_type type = cmark_node_get_type(node);
	if (type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE) {
		const char *lit = cmark_node_get_literal(node);
		if (lit) {
			smart_str_appends(out, lit);
		}
		return;
	}
	{
		cmark_node *child;
		for (child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
			php_mdhtml_collect_text(child, out);
		}
	}
}

/* Growable list of per-heading ids, built in document order during the
 * pre-render tree walk, consumed in the same order while string-
 * postprocessing cmark's HTML output (see php_mdhtml_postprocess). */
typedef struct {
	zend_string **ids;
	size_t count;
	size_t capacity;
} php_mdhtml_id_list;

static void php_mdhtml_id_list_push(php_mdhtml_id_list *list, zend_string *id) {
	if (list->count == list->capacity) {
		list->capacity = list->capacity ? list->capacity * 2 : 8;
		list->ids = erealloc(list->ids, list->capacity * sizeof(zend_string *));
	}
	list->ids[list->count++] = id;
}

static void php_mdhtml_id_list_destroy(php_mdhtml_id_list *list) {
	size_t i;
	for (i = 0; i < list->count; i++) {
		zend_string_release(list->ids[i]);
	}
	if (list->ids) {
		efree(list->ids);
	}
}

/* Computes each heading's id (custom `{#id}` override if present, else an
 * auto-slug of its plain text -- matching MD::'s STEP 7b/8 heading id
 * logic) without mutating the tree; the trailing `{#id}` marker (if any)
 * is stripped later, in the HTML string, by php_mdhtml_postprocess(). */
static void php_mdhtml_compute_heading_ids(cmark_node *doc, php_mdhtml_id_list *out) {
	cmark_iter *iter = cmark_iter_new(doc);
	cmark_event_type ev;

	while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cmark_node *node = cmark_iter_get_node(iter);
		if (ev != CMARK_EVENT_ENTER || cmark_node_get_type(node) != CMARK_NODE_HEADING) {
			continue;
		}

		{
			smart_str text = {0};
			zend_string *id;
			php_mdhtml_collect_text(node, &text);
			smart_str_0(&text);

			id = NULL;
			if (text.s && ZSTR_LEN(text.s) > 3) {
				const char *s = ZSTR_VAL(text.s);
				size_t len = ZSTR_LEN(text.s);
				if (s[len - 1] == '}') {
					/* scan back over id chars until '#', then require '{'
					 * right before it -- format is "{#id}", so the '#' is
					 * NOT itself an id char and must not be swallowed by
					 * the word-char scan (that was the original bug: the
					 * scan continued past '#' looking for '{', so a
					 * non-word-char check on '#' itself always failed). */
					size_t i = len - 2;
					int ok = 1;
					while (i > 0 && s[i] != '#') {
						unsigned char c = (unsigned char) s[i];
						if (!(php_mdhtml_is_word_char(c) || c == '-' || c == ':' || c == '.')) {
							ok = 0;
							break;
						}
						i--;
					}
					if (ok && i > 0 && s[i] == '#' && s[i - 1] == '{') {
						size_t idstart = i + 1;
						size_t idlen = (len - 1) - idstart;
						if (idlen > 0) {
							id = zend_string_init(s + idstart, idlen, 0);
						}
					}
				}
			}

			if (!id) {
				id = text.s ? php_mdhtml_slugify(ZSTR_VAL(text.s), ZSTR_LEN(text.s)) : ZSTR_EMPTY_ALLOC();
			}

			smart_str_free(&text);
			php_mdhtml_id_list_push(out, id);
		}
	}

	cmark_iter_free(iter);
}

/* ========================================================================
 * HTML STRING POST-PROCESSING
 *
 * A single linear pass over cmark_render_html()'s output doing everything
 * that has no clean per-node hook in cmark-gfm's own HTML renderer for a
 * *built-in* node type (only syntax_extensions get a custom render
 * callback for their *own* node types -- headings/links/images/list items
 * are all built-in):
 *
 *  - inject `id="..."` on every <h1>-<h6> (from php_mdhtml_compute_heading_ids,
 *    matched by occurrence order) and strip a trailing `{#id}` marker
 *    left in the heading's rendered text by the custom-id syntax.
 *  - `target="_blank" rel="noopener noreferrer"` on external (http/https)
 *    links, matching MD::'s buildLink().
 *  - `loading="lazy"` on images, matching MD::'s image output.
 *  - task-list `<li>` gets `class="task-item"` and its checkbox's
 *    `checked=""`/`disabled=""` (cmark-gfm's HTML-boolean-attribute style)
 *    become bare `checked`/`disabled` (MD::'s style). NOT done here -- as
 *    a mostly-cosmetic gap, documented in CLAUDE.md -- is wrapping the
 *    *unordered list itself* in `class="task-list"`, since that would
 *    need lookahead/backpatching across the whole `<ul>...</ul>` this
 *    single forward pass doesn't do.
 * ======================================================================== */
static zend_string *php_mdhtml_postprocess(const char *html, size_t len, php_mdhtml_id_list *heading_ids) {
	smart_str out = {0};
	size_t i = 0;
	size_t heading_idx = 0;

	while (i < len) {
		/* <h1> .. <h6> opening tag */
		if (i + 3 < len && html[i] == '<' && html[i + 1] == 'h'
			&& html[i + 2] >= '1' && html[i + 2] <= '6' && html[i + 3] == '>') {
			char level = html[i + 2];
			smart_str_appendc(&out, '<');
			smart_str_appendc(&out, 'h');
			smart_str_appendc(&out, level);
			if (heading_ids && heading_idx < heading_ids->count) {
				smart_str_appends(&out, " id=\"");
				smart_str_append(&out, heading_ids->ids[heading_idx]);
				smart_str_appendc(&out, '"');
			}
			smart_str_appendc(&out, '>');
			heading_idx++;
			i += 4;

			/* Find this heading's closing tag and copy its inner HTML,
			 * stripping a trailing `{#id}` marker if present (it was only
			 * *read*, not removed, by php_mdhtml_compute_heading_ids). */
			{
				char closing[6];
				size_t closing_len = (size_t) snprintf(closing, sizeof(closing), "</h%c>", level);
				size_t inner_start = i;
				size_t inner_end = inner_start;
				while (inner_end < len && !(inner_end + closing_len <= len
						&& memcmp(html + inner_end, closing, closing_len) == 0)) {
					inner_end++;
				}
				{
					size_t seg_len = inner_end - inner_start;
					const char *seg = html + inner_start;
					/* strip trailing "{#...}" (and one preceding space) --
					 * same fixed scan as php_mdhtml_compute_heading_ids:
					 * stop at '#', then require '{' right before it. */
					if (seg_len > 3 && seg[seg_len - 1] == '}') {
						size_t j = seg_len - 2;
						int ok = 1;
						while (j > 0 && seg[j] != '#') {
							unsigned char c = (unsigned char) seg[j];
							if (!(php_mdhtml_is_word_char(c) || c == '-' || c == ':' || c == '.')) {
								ok = 0;
								break;
							}
							j--;
						}
						if (ok && j > 0 && seg[j] == '#' && seg[j - 1] == '{') {
							j--; /* now at '{' */
							if (j > 0 && seg[j - 1] == ' ') {
								j--;
							}
							seg_len = j;
						}
					}
					smart_str_appendl(&out, seg, seg_len);
				}
				i = inner_end;
			}
			continue;
		}

		/* <a href="http... external link */
		if (i + 9 < len && memcmp(html + i, "<a href=\"", 9) == 0
			&& (memcmp(html + i + 9, "http://", 7) == 0 || memcmp(html + i + 9, "https://", 8) == 0)) {
			size_t j = i + 9;
			while (j < len && html[j] != '"') j++;
			if (j < len) j++; /* past closing quote */
			smart_str_appendl(&out, html + i, j - i);
			smart_str_appends(&out, " target=\"_blank\" rel=\"noopener noreferrer\"");
			i = j;
			continue;
		}

		/* <img ...> -- add loading="lazy" right after the tag name */
		if (i + 5 <= len && memcmp(html + i, "<img ", 5) == 0) {
			smart_str_appends(&out, "<img loading=\"lazy\" ");
			i += 5;
			continue;
		}

		/* <ul> that opens a GFM task list -- kirigami's own CSS
		 * (packages/canva prose.scss) has real, load-bearing rules for
		 * `.task-list` (list-style: none; padding-left: 0), so without
		 * this the browser's default bullet would show *next to* the
		 * checkbox -- not just a cosmetic gap. Pure lookahead (peek past
		 * the one newline cmark's pretty-printer puts after `<ul>`, don't
		 * consume it) at the tag *immediately* following: cmark-gfm's own
		 * tasklist extension always renders every item of a task list the
		 * same way, so checking the first child is sufficient, same as
		 * MD::'s own "wrap a run of task-item <li>s" behavior. */
		if (i + 4 <= len && memcmp(html + i, "<ul>", 4) == 0) {
			size_t j = i + 4;
			if (j < len && html[j] == '\n') j++;
			if (j + 26 <= len && memcmp(html + j, "<li><input type=\"checkbox\"", 26) == 0) {
				smart_str_appends(&out, "<ul class=\"task-list\">");
				i += 4;
				continue;
			}
		}

		/* <li><input type="checkbox" -- GFM task item. The literal below
		 * is 26 bytes (not 27 -- an earlier off-by-one here compared one
		 * byte past the string into its NUL terminator, so this branch
		 * silently never matched real output even though the class
		 * injection code was otherwise correct). */
		if (i + 26 <= len && memcmp(html + i, "<li><input type=\"checkbox\"", 26) == 0) {
			smart_str_appends(&out, "<li class=\"task-item\"><input type=\"checkbox\"");
			i += 26;
			continue;
		}
		if (i + 10 <= len && memcmp(html + i, "checked=\"\"", 10) == 0) {
			smart_str_appends(&out, "checked");
			i += 10;
			continue;
		}
		if (i + 11 <= len && memcmp(html + i, "disabled=\"\"", 11) == 0) {
			smart_str_appends(&out, "disabled");
			i += 11;
			continue;
		}

		smart_str_appendc(&out, html[i]);
		i++;
	}

	smart_str_0(&out);
	return out.s ? out.s : ZSTR_EMPTY_ALLOC();
}

/* ========================================================================
 * RAW HTML WHITELIST SANITIZER
 *
 * Ported from MD::sanitizeHtmlTag()/isSafeUrl() (kirigami/php-prepros).
 * Applied directly to CMARK_NODE_HTML_BLOCK / CMARK_NODE_HTML_INLINE node
 * literals via cmark_node_set_literal() *before* cmark_render_html() is
 * called -- precise (touches only nodes holding raw user-written HTML,
 * never HTML cmark itself generates for other constructs) rather than a
 * whole-document regex pass. CMARK_OPT_UNSAFE lets these nodes' raw
 * content survive into the tree unescaped in the first place; without
 * this pass they would pass straight through untouched.
 * ======================================================================== */

static const char *const php_mdhtml_html_allowed_tags[] = {
	"div", "span", "p", "br", "hr", "wbr",
	"b", "strong", "i", "em", "u", "s", "strike", "del", "ins",
	"mark", "small", "sub", "sup", "kbd", "code", "pre", "abbr", "q", "cite",
	"ul", "ol", "li", "dl", "dt", "dd",
	"table", "thead", "tbody", "tfoot", "tr", "td", "th", "caption", "colgroup", "col",
	"blockquote",
	"a", "img", "picture", "source", "figure", "figcaption",
	"h1", "h2", "h3", "h4", "h5", "h6",
	"details", "summary", "center",
	NULL
};

static const char *const php_mdhtml_html_global_attrs[] = {
	"id", "class", "title", "align", "valign", "width", "height", "dir", "lang", NULL
};

typedef struct {
	const char *tag;
	const char *const *attrs;
} php_mdhtml_tag_attrs_entry;

static const char *const php_mdhtml_attrs_a[] = {"href", "name", "target", "rel", NULL};
static const char *const php_mdhtml_attrs_img[] = {"src", "alt", "loading", "srcset", "sizes", NULL};
static const char *const php_mdhtml_attrs_source[] = {"src", "srcset", "type", "media", NULL};
static const char *const php_mdhtml_attrs_td[] = {"colspan", "rowspan", NULL};
static const char *const php_mdhtml_attrs_th[] = {"colspan", "rowspan", "scope", NULL};
static const char *const php_mdhtml_attrs_col[] = {"span", NULL};
static const char *const php_mdhtml_attrs_ol[] = {"start", "type", NULL};
static const char *const php_mdhtml_attrs_details[] = {"open", NULL};

static const php_mdhtml_tag_attrs_entry php_mdhtml_html_tag_attrs[] = {
	{"a", php_mdhtml_attrs_a}, {"img", php_mdhtml_attrs_img}, {"source", php_mdhtml_attrs_source},
	{"td", php_mdhtml_attrs_td}, {"th", php_mdhtml_attrs_th}, {"col", php_mdhtml_attrs_col},
	{"ol", php_mdhtml_attrs_ol}, {"details", php_mdhtml_attrs_details},
};

static const char *const php_mdhtml_html_void_tags[] = {
	"img", "br", "hr", "wbr", "source", "col", NULL
};

static const char *const php_mdhtml_html_dangerous_tags[] = {
	"script", "style", "iframe", "object", "embed", "noscript", "template",
	"form", "button", "textarea", "select", "option", NULL
};

static int php_mdhtml_str_in_list(const char *needle, size_t len, const char *const *list) {
	size_t i;
	for (i = 0; list[i]; i++) {
		if (strlen(list[i]) == len && strncasecmp(list[i], needle, len) == 0) {
			return 1;
		}
	}
	return 0;
}

static const char *const *php_mdhtml_attrs_for_tag(const char *tag, size_t len) {
	size_t i;
	for (i = 0; i < sizeof(php_mdhtml_html_tag_attrs) / sizeof(php_mdhtml_html_tag_attrs[0]); i++) {
		if (strlen(php_mdhtml_html_tag_attrs[i].tag) == len
			&& strncasecmp(php_mdhtml_html_tag_attrs[i].tag, tag, len) == 0) {
			return php_mdhtml_html_tag_attrs[i].attrs;
		}
	}
	return NULL;
}

/* data:image/(png|gif|jpe?g|webp);base64, only -- same rationale as
 * MD::isSafeUrl(): keeps data:image/svg+xml (can embed <script>) and
 * data:text/html out. */
static int php_mdhtml_is_safe_data_url(const char *url, size_t len) {
	static const char *const safe[] = {
		"data:image/png;base64,", "data:image/gif;base64,",
		"data:image/jpeg;base64,", "data:image/jpg;base64,",
		"data:image/webp;base64,", NULL
	};
	size_t i;
	for (i = 0; safe[i]; i++) {
		size_t slen = strlen(safe[i]);
		if (len >= slen && strncasecmp(url, safe[i], slen) == 0) {
			return 1;
		}
	}
	return 0;
}

static int php_mdhtml_is_safe_url(const char *url, size_t len) {
	size_t i;
	if (len == 0) return 1;
	/* find "scheme:" -- an unescaped ':' before any '/' means a scheme is
	 * present; anything else (relative path, #anchor, bare "x") is safe. */
	for (i = 0; i < len; i++) {
		if (url[i] == ':') break;
		if (url[i] == '/' || url[i] == '#' || url[i] == '?') {
			return 1; /* no scheme before a path/anchor/query char */
		}
	}
	if (i == len) return 1; /* no ':' at all */

	if (php_mdhtml_str_in_list(url, i, (const char *const[]){"http", "https", "mailto", "tel", NULL})) {
		return 1;
	}
	if (i == 4 && strncasecmp(url, "data", 4) == 0) {
		return php_mdhtml_is_safe_data_url(url, len);
	}
	return 0;
}

/* Sanitizes one raw HTML tag (e.g. `<div align="center">`, `</div>`,
 * `<img src="..." onerror="...">`). Returns a newly allocated,
 * NUL-terminated string (caller efree()s it) -- empty if the tag should
 * be stripped silently. cmark already separated "this is a raw HTML
 * node" from "this is text" during parsing, so unlike MD::'s own inline
 * scanner there is no plain-text fallback to preserve here -- an invalid
 * tag is simply dropped. */
static zend_string *php_mdhtml_sanitize_tag(const char *tag, size_t len) {
	size_t i = 1; /* skip '<' */
	int closing = 0;
	size_t name_start, name_len;
	smart_str out = {0};
	const char *const *allowed_attrs_extra;

	if (len < 2 || tag[0] != '<' || tag[len - 1] != '>') {
		return NULL;
	}
	if (tag[i] == '/') {
		closing = 1;
		i++;
	}
	name_start = i;
	while (i < len && (isalnum((unsigned char) tag[i]) || tag[i] == '-')) i++;
	name_len = i - name_start;
	if (name_len == 0) {
		return NULL;
	}

	if (!php_mdhtml_str_in_list(tag + name_start, name_len, php_mdhtml_html_allowed_tags)) {
		return NULL;
	}

	if (closing) {
		smart_str_appendc(&out, '<');
		smart_str_appendc(&out, '/');
		smart_str_appendl(&out, tag + name_start, name_len);
		smart_str_appendc(&out, '>');
		smart_str_0(&out);
		return out.s;
	}

	smart_str_appendc(&out, '<');
	smart_str_appendl(&out, tag + name_start, name_len);

	allowed_attrs_extra = php_mdhtml_attrs_for_tag(tag + name_start, name_len);

	/* end of tag name reached; parse attributes up to the trailing '/'
	 * (if self-closing) or '>' */
	while (i < len) {
		size_t attr_start, attr_len, val_start = 0, val_len = 0;
		char quote = 0;

		while (i < len && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\n' || tag[i] == '\r')) i++;
		if (i >= len || tag[i] == '/' || tag[i] == '>') break;

		attr_start = i;
		while (i < len && (isalnum((unsigned char) tag[i]) || tag[i] == '_' || tag[i] == ':' || tag[i] == '-' || tag[i] == '.')) i++;
		attr_len = i - attr_start;
		if (attr_len == 0) {
			i++; /* avoid an infinite loop on a stray char */
			continue;
		}

		while (i < len && (tag[i] == ' ' || tag[i] == '\t')) i++;
		if (i < len && tag[i] == '=') {
			i++;
			while (i < len && (tag[i] == ' ' || tag[i] == '\t')) i++;
			if (i < len && (tag[i] == '"' || tag[i] == '\'')) {
				quote = tag[i];
				i++;
				val_start = i;
				while (i < len && tag[i] != quote) i++;
				val_len = i - val_start;
				if (i < len) i++; /* past closing quote */
			} else {
				val_start = i;
				while (i < len && tag[i] != ' ' && tag[i] != '\t' && tag[i] != '>' && tag[i] != '/') i++;
				val_len = i - val_start;
			}
		}

		/* onXXX handlers rejected regardless of any whitelist */
		if (attr_len >= 2 && (tag[attr_start] == 'o' || tag[attr_start] == 'O')
			&& (tag[attr_start + 1] == 'n' || tag[attr_start + 1] == 'N')) {
			continue;
		}

		if (!php_mdhtml_str_in_list(tag + attr_start, attr_len, php_mdhtml_html_global_attrs)
			&& !(allowed_attrs_extra && php_mdhtml_str_in_list(tag + attr_start, attr_len, allowed_attrs_extra))) {
			continue;
		}

		if (attr_len == 4 && strncasecmp(tag + attr_start, "href", 4) == 0) {
			if (!php_mdhtml_is_safe_url(tag + val_start, val_len)) continue;
		}
		if (attr_len == 3 && strncasecmp(tag + attr_start, "src", 3) == 0) {
			if (!php_mdhtml_is_safe_url(tag + val_start, val_len)) continue;
		}

		smart_str_appendc(&out, ' ');
		smart_str_appendl(&out, tag + attr_start, attr_len);
		if (val_start || val_len) {
			smart_str_appends(&out, "=\"");
			/* values were already HTML text when cmark parsed them out of
			 * the raw literal, so no further escaping is applied here,
			 * matching MD::'s own sanitizeHtmlTag() (htmlspecialchars is
			 * only applied there because PHP's regex worked on the whole,
			 * still-unescaped document -- here cmark has already isolated
			 * this literal as a standalone raw-HTML node). */
			smart_str_appendl(&out, tag + val_start, val_len);
			smart_str_appendc(&out, '"');
		}
	}

	if (php_mdhtml_str_in_list(tag + name_start, name_len, php_mdhtml_html_void_tags)) {
		smart_str_appends(&out, " /");
	}
	smart_str_appendc(&out, '>');
	smart_str_0(&out);
	return out.s;
}

/* Sanitizes a whole HTML_BLOCK/HTML_INLINE literal, which may contain
 * several tags plus text between them (an HTML block in particular is
 * often several lines/tags at once). Dangerous tags are stripped along
 * with everything up to their matching closing tag; comments are
 * dropped; any other tag goes through php_mdhtml_sanitize_tag(); text
 * between tags passes through unchanged (this literal IS the raw HTML
 * the user wrote, kept as-is by CMARK_OPT_UNSAFE, so no additional
 * escaping is applied to the parts that are genuinely markup). */
static zend_string *php_mdhtml_sanitize_html_literal(const char *html, size_t len) {
	smart_str out = {0};
	size_t i = 0;

	while (i < len) {
		if (html[i] != '<') {
			smart_str_appendc(&out, html[i]);
			i++;
			continue;
		}

		if (i + 4 <= len && memcmp(html + i, "<!--", 4) == 0) {
			size_t j = i + 4;
			while (j + 3 <= len && memcmp(html + j, "-->", 3) != 0) j++;
			i = (j + 3 <= len) ? j + 3 : len;
			continue;
		}

		{
			size_t tag_start = i;
			size_t j = i + 1;
			int is_closing = (j < len && html[j] == '/');
			size_t name_start = is_closing ? j + 1 : j;
			size_t name_len = 0;
			while (name_start + name_len < len
				&& (isalnum((unsigned char) html[name_start + name_len]) || html[name_start + name_len] == '-')) {
				name_len++;
			}

			if (name_len == 0) {
				/* not a tag after all ('<' used literally) */
				smart_str_appendc(&out, '<');
				i++;
				continue;
			}

			while (j < len && html[j] != '>') j++;
			if (j < len) j++; /* include '>' */

			if (!is_closing && php_mdhtml_str_in_list(html + name_start, name_len, php_mdhtml_html_dangerous_tags)) {
				/* strip up to and including the matching closing tag */
				smart_str out_close = {0};
				smart_str_appends(&out_close, "</");
				smart_str_appendl(&out_close, html + name_start, name_len);
				smart_str_appendc(&out_close, '>');
				smart_str_0(&out_close);
				{
					const char *needle = ZSTR_VAL(out_close.s);
					size_t needle_len = ZSTR_LEN(out_close.s);
					size_t k = j;
					while (k + needle_len <= len && strncasecmp(html + k, needle, needle_len) != 0) k++;
					i = (k + needle_len <= len) ? k + needle_len : len;
				}
				smart_str_free(&out_close);
				continue;
			}

			{
				zend_string *sanitized = php_mdhtml_sanitize_tag(html + tag_start, j - tag_start);
				if (sanitized) {
					smart_str_append(&out, sanitized);
					zend_string_release(sanitized);
				}
			}
			i = j;
		}
	}

	smart_str_0(&out);
	return out.s ? out.s : ZSTR_EMPTY_ALLOC();
}

static void php_mdhtml_sanitize_raw_html_nodes(cmark_node *doc) {
	cmark_iter *iter = cmark_iter_new(doc);
	cmark_event_type ev;

	while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cmark_node *node = cmark_iter_get_node(iter);
		cmark_node_type type;
		if (ev != CMARK_EVENT_ENTER) continue;
		type = cmark_node_get_type(node);
		if (type == CMARK_NODE_HTML_BLOCK || type == CMARK_NODE_HTML_INLINE) {
			const char *lit = cmark_node_get_literal(node);
			if (lit) {
				zend_string *sanitized = php_mdhtml_sanitize_html_literal(lit, strlen(lit));
				cmark_node_set_literal(node, ZSTR_VAL(sanitized));
				zend_string_release(sanitized);
			}
		}
	}

	cmark_iter_free(iter);
}

/* ========================================================================
 * EMOJI SUBSTITUTION (`:shortcode:` -> unicode)
 *
 * Pure text -> text substitution, applied directly to CMARK_NODE_TEXT
 * literals (never CODE/HTML -- those are different node types, so this
 * cannot touch code spans/blocks or raw HTML the way a whole-document
 * regex would need to be careful about). Unknown shortcodes are left
 * as-is, matching MD::emojiFor()'s fallback.
 * ======================================================================== */
static int php_mdhtml_emoji_lookup(const char *name, size_t len, zend_string **out) {
	void *found;
	zend_string *key = zend_string_init(name, len, 0);
	zend_str_tolower(ZSTR_VAL(key), ZSTR_LEN(key));

	found = zend_hash_find_ptr(&MDHTML_G(emoji_custom), key);
	if (!found) {
		found = zend_hash_find_ptr(&MDHTML_G(emoji_builtin), key);
	}
	zend_string_release(key);
	if (found) {
		*out = (zend_string *) found;
		return 1;
	}
	return 0;
}

static void php_mdhtml_process_emoji_text(cmark_node *text_node) {
	const char *lit = cmark_node_get_literal(text_node);
	size_t len, i;
	smart_str out = {0};
	int replaced = 0;

	if (!lit) return;
	len = strlen(lit);

	for (i = 0; i < len; i++) {
		if (lit[i] == ':') {
			size_t j = i + 1;
			while (j < len && (isalnum((unsigned char) lit[j]) || lit[j] == '_' || lit[j] == '+' || lit[j] == '-')) j++;
			if (j < len && lit[j] == ':' && j > i + 1) {
				zend_string *emoji;
				if (php_mdhtml_emoji_lookup(lit + i + 1, j - i - 1, &emoji)) {
					smart_str_append(&out, emoji);
					replaced = 1;
					i = j; /* loop's i++ lands past the closing ':' */
					continue;
				}
			}
		}
		smart_str_appendc(&out, lit[i]);
	}

	if (replaced) {
		smart_str_0(&out);
		cmark_node_set_literal(text_node, ZSTR_VAL(out.s));
	}
	smart_str_free(&out);
}

static void php_mdhtml_process_emoji(cmark_node *doc) {
	/* Collect first, mutate after: cmark's own iterator docs warn against
	 * mutating the node the iterator currently points to. None of these
	 * mutations change tree *structure* (only a TEXT node's literal), so
	 * this particular pass would likely be safe to mutate in-place, but
	 * collecting first keeps the same safe pattern every tree-walking
	 * pass in this file uses -- one fewer thing to get subtly wrong. */
	cmark_node **nodes = NULL;
	size_t count = 0, capacity = 0;
	cmark_iter *iter = cmark_iter_new(doc);
	cmark_event_type ev;

	while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cmark_node *node = cmark_iter_get_node(iter);
		if (ev == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_TEXT) {
			if (count == capacity) {
				capacity = capacity ? capacity * 2 : 16;
				nodes = erealloc(nodes, capacity * sizeof(cmark_node *));
			}
			nodes[count++] = node;
		}
	}
	cmark_iter_free(iter);

	{
		size_t i;
		for (i = 0; i < count; i++) {
			php_mdhtml_process_emoji_text(nodes[i]);
		}
	}
	if (nodes) efree(nodes);
}

/* ========================================================================
 * EXTENDED INLINE SYNTAX: ==highlight==, ^superscript^, ~subscript~
 *
 * None of these are CommonMark or GFM; MD:: implements them as three
 * plain regex passes over already-escaped HTML text. Here, each is a
 * TEXT-node split-and-wrap: the matched span becomes a
 * CMARK_NODE_CUSTOM_INLINE node (cmark's own "opaque literal HTML
 * fragment" inline node -- no new cmark_syntax_extension needed) whose
 * on_enter/on_exit are the literal open/close tags, wrapping a TEXT child
 * with the captured content; the original TEXT node is split into
 * [before, wrap, after...] around each match.
 *
 * `~~strikethrough~~` never reaches the subscript pass as leftover single
 * tildes: cmark-gfm's own strikethrough extension already consumes real
 * `~~...~~` spans *during parsing*, before any of these passes run, so
 * whatever single `~` survives in a TEXT node by the time this runs is
 * genuinely single-tilde usage -- no manual precedence juggling needed
 * between the two, unlike MD::'s regex passes which must explicitly order
 * strikethrough before subscript.
 * ======================================================================== */
static void php_mdhtml_set_literal_range(cmark_node *node, const char *start, size_t len) {
	char *buf = emalloc(len + 1);
	memcpy(buf, start, len);
	buf[len] = '\0';
	cmark_node_set_literal(node, buf);
	efree(buf);
}

static void php_mdhtml_wrap_delim(cmark_node *text_node, const char *delim, size_t delim_len, const char *open_tag, const char *close_tag) {
	const char *lit = cmark_node_get_literal(text_node);
	size_t len, i = 0, seg_start = 0;
	int replaced = 0;

	if (!lit) return;
	len = strlen(lit);

	while (i + delim_len <= len) {
		if (memcmp(lit + i, delim, delim_len) == 0) {
			size_t j = i + delim_len;
			while (j + delim_len <= len && memcmp(lit + j, delim, delim_len) != 0 && lit[j] != '\n') j++;
			if (j + delim_len <= len && memcmp(lit + j, delim, delim_len) == 0 && j > i + delim_len) {
				cmark_node *wrap, *inner;

				if (i > seg_start) {
					cmark_node *before = cmark_node_new(CMARK_NODE_TEXT);
					php_mdhtml_set_literal_range(before, lit + seg_start, i - seg_start);
					cmark_node_insert_before(text_node, before);
				}

				wrap = cmark_node_new(CMARK_NODE_CUSTOM_INLINE);
				cmark_node_set_on_enter(wrap, open_tag);
				cmark_node_set_on_exit(wrap, close_tag);
				inner = cmark_node_new(CMARK_NODE_TEXT);
				php_mdhtml_set_literal_range(inner, lit + i + delim_len, j - (i + delim_len));
				cmark_node_append_child(wrap, inner);
				cmark_node_insert_before(text_node, wrap);

				replaced = 1;
				seg_start = j + delim_len;
				i = j + delim_len;
				continue;
			}
		}
		i++;
	}

	if (replaced) {
		if (seg_start < len) {
			cmark_node *after = cmark_node_new(CMARK_NODE_TEXT);
			php_mdhtml_set_literal_range(after, lit + seg_start, len - seg_start);
			cmark_node_insert_before(text_node, after);
		}
		cmark_node_unlink(text_node);
		cmark_node_free(text_node);
	}
}

static void php_mdhtml_apply_delim_pass(cmark_node *doc, const char *delim, size_t delim_len, const char *open_tag, const char *close_tag) {
	cmark_node **nodes = NULL;
	size_t count = 0, capacity = 0;
	cmark_iter *iter = cmark_iter_new(doc);
	cmark_event_type ev;

	while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cmark_node *node = cmark_iter_get_node(iter);
		if (ev == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_TEXT) {
			if (count == capacity) {
				capacity = capacity ? capacity * 2 : 16;
				nodes = erealloc(nodes, capacity * sizeof(cmark_node *));
			}
			nodes[count++] = node;
		}
	}
	cmark_iter_free(iter);

	{
		size_t i;
		for (i = 0; i < count; i++) {
			php_mdhtml_wrap_delim(nodes[i], delim, delim_len, open_tag, close_tag);
		}
	}
	if (nodes) efree(nodes);
}

/* ========================================================================
 * GFM-STYLE ALERTS (`> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`,
 * `[!CAUTION]`)
 *
 * GitHub's own convention, not part of cmark-gfm (or GFM itself) -- it
 * parses as a plain blockquote whose first paragraph starts with the
 * literal text "[!NOTE]" etc. Detected as a tree transform (unlike
 * headings/links/tasklist, which are cheap string patches): a
 * blockquote's rendering has to change from `<blockquote>` to a
 * `<div class="markdown-alert ...">` with an injected title paragraph, and
 * cmark's HTML renderer has no per-node-instance hook for a built-in type
 * like CMARK_NODE_BLOCK_QUOTE. Solved by rendering the (marker-stripped)
 * blockquote's content to HTML *early* (via a scratch CMARK_NODE_DOCUMENT
 * holding its stolen children, using the same options/extensions as the
 * real, outer render so nested tables/tasklists/etc still work), building
 * the final wrapped HTML by hand, and swapping the whole blockquote for a
 * single CMARK_NODE_HTML_BLOCK holding that literal -- which, under
 * CMARK_OPT_UNSAFE, survives into the real render completely untouched.
 *
 * Processed in *reverse* document order on purpose: a nested blockquote
 * (an alert inside another blockquote, or an alert inside an alert) is
 * converted first, so that when its ancestor is processed next, moving
 * the ancestor's children into a scratch document either doesn't touch
 * the nested one at all, or picks it up as an already-finished
 * CMARK_NODE_HTML_BLOCK (rendered verbatim, correctly nested) -- not as a
 * cmark_node this code has already freed. Processing outer-to-inner
 * instead would free the inner blockquote out from under this function's
 * own collected node list before it's reached, a real use-after-free.
 * ======================================================================== */
typedef struct {
	const char *marker;
	const char *class_suffix;
	const char *label;
} php_mdhtml_alert_type;

static const php_mdhtml_alert_type php_mdhtml_alert_types[] = {
	{"[!NOTE]", "note", "NOTE"},
	{"[!TIP]", "tip", "TIP"},
	{"[!IMPORTANT]", "important", "IMPORTANT"},
	{"[!WARNING]", "warning", "WARNING"},
	{"[!CAUTION]", "caution", "CAUTION"},
};

static zend_string *php_mdhtml_render_alert_body(cmark_node *blockquote, int options, cmark_llist *extensions) {
	cmark_node *temp_doc = cmark_node_new(CMARK_NODE_DOCUMENT);
	cmark_node *child;
	char *inner_html;
	zend_string *result;

	while ((child = cmark_node_first_child(blockquote)) != NULL) {
		cmark_node_unlink(child);
		cmark_node_append_child(temp_doc, child);
	}

	inner_html = cmark_render_html(temp_doc, options, extensions);
	result = inner_html ? zend_string_init(inner_html, strlen(inner_html), 0) : ZSTR_EMPTY_ALLOC();
	if (inner_html) free(inner_html);
	cmark_node_free(temp_doc);
	return result;
}

static void php_mdhtml_process_alerts(cmark_node *doc, int options, cmark_llist *extensions) {
	cmark_node **blockquotes = NULL;
	size_t count = 0, capacity = 0;
	cmark_iter *iter = cmark_iter_new(doc);
	cmark_event_type ev;

	while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cmark_node *node = cmark_iter_get_node(iter);
		if (ev == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_BLOCK_QUOTE) {
			if (count == capacity) {
				capacity = capacity ? capacity * 2 : 8;
				blockquotes = erealloc(blockquotes, capacity * sizeof(cmark_node *));
			}
			blockquotes[count++] = node;
		}
	}
	cmark_iter_free(iter);

	{
		size_t idx;
		for (idx = count; idx-- > 0; ) {
			cmark_node *bq = blockquotes[idx];
			cmark_node *first_para = cmark_node_first_child(bq);
			cmark_node *marker_text;
			const char *lit;
			int type_idx = -1;
			size_t t;

			if (!first_para || cmark_node_get_type(first_para) != CMARK_NODE_PARAGRAPH) continue;
			marker_text = cmark_node_first_child(first_para);
			if (!marker_text || cmark_node_get_type(marker_text) != CMARK_NODE_TEXT) continue;
			lit = cmark_node_get_literal(marker_text);
			if (!lit) continue;

			for (t = 0; t < sizeof(php_mdhtml_alert_types) / sizeof(php_mdhtml_alert_types[0]); t++) {
				if (strcmp(lit, php_mdhtml_alert_types[t].marker) == 0) {
					type_idx = (int) t;
					break;
				}
			}
			if (type_idx < 0) continue;

			{
				cmark_node *after_marker = cmark_node_next(marker_text);
				cmark_node *html_block;
				smart_str final_html = {0};
				zend_string *inner;

				cmark_node_unlink(marker_text);
				cmark_node_free(marker_text);
				if (after_marker && (cmark_node_get_type(after_marker) == CMARK_NODE_SOFTBREAK
						|| cmark_node_get_type(after_marker) == CMARK_NODE_LINEBREAK)) {
					cmark_node_unlink(after_marker);
					cmark_node_free(after_marker);
				}
				if (!cmark_node_first_child(first_para)) {
					cmark_node_unlink(first_para);
					cmark_node_free(first_para);
				}

				inner = php_mdhtml_render_alert_body(bq, options, extensions);

				smart_str_appends(&final_html, "<div class=\"markdown-alert markdown-alert-");
				smart_str_appends(&final_html, php_mdhtml_alert_types[type_idx].class_suffix);
				smart_str_appends(&final_html, "\"><p class=\"markdown-alert-title\">");
				smart_str_appends(&final_html, php_mdhtml_alert_types[type_idx].label);
				smart_str_appends(&final_html, "</p>");
				smart_str_append(&final_html, inner);
				smart_str_appends(&final_html, "</div>");
				smart_str_0(&final_html);
				zend_string_release(inner);

				html_block = cmark_node_new(CMARK_NODE_HTML_BLOCK);
				cmark_node_set_literal(html_block, ZSTR_VAL(final_html.s));
				smart_str_free(&final_html);

				cmark_node_replace(bq, html_block);
				cmark_node_free(bq);
			}
		}
	}
	if (blockquotes) efree(blockquotes);
}

/* ========================================================================
 * DEFINITION LISTS (extended syntax)
 *   Term
 *   : Definition
 * Not CommonMark: with no blank line between them, "Term" and
 * ": Definition" are just two soft-wrapped lines of the *same* paragraph
 * as far as cmark is concerned (there's nothing block-level about a
 * leading ':' to make it a separate block), so unlike alerts this can't
 * be caught as a distinct node in the tree -- cmark has already merged
 * them into one <p>text\n: text</p> by the time this runs. Implemented
 * as a post-render string pass instead (php_mdhtml_process_definition_lists,
 * run after php_mdhtml_postprocess()): scans for a `<p>` whose content,
 * split on literal '\n', is a first "term" line followed by one or more
 * ':'-prefixed "definition" lines, and rewrites it to a `<dl>`; consecutive
 * such `<p>`s (separated only by whitespace) merge into one `<dl>`, the
 * same continuation behavior MD::extractDefinitionLists() implements.
 * ======================================================================== */
static int php_mdhtml_is_colon_line(const char *s, size_t len) {
	size_t i = 0;
	while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
	if (i >= len || s[i] != ':') return 0;
	i++;
	if (i >= len || !(s[i] == ' ' || s[i] == '\t')) return 0;
	while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
	return i < len;
}

static void php_mdhtml_colon_line_content(const char *s, size_t len, const char **out, size_t *out_len) {
	size_t i = 0;
	while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
	i++; /* ':' */
	while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
	*out = s + i;
	*out_len = len - i;
}

static zend_string *php_mdhtml_process_definition_lists(zend_string *html) {
	smart_str out = {0};
	const char *s = ZSTR_VAL(html);
	size_t len = ZSTR_LEN(html);
	size_t i = 0;

	while (i < len) {
		if (i + 3 <= len && memcmp(s + i, "<p>", 3) == 0) {
			size_t scan = i;
			smart_str dl_body = {0};
			int matched_any = 0;
			size_t after_all = i;

			while (scan + 3 <= len && memcmp(s + scan, "<p>", 3) == 0) {
				size_t content_start = scan + 3;
				size_t content_end = content_start;
				size_t first_line_end = (size_t) -1;
				size_t k;

				while (content_end < len && !(content_end + 4 <= len && memcmp(s + content_end, "</p>", 4) == 0)) {
					content_end++;
				}
				if (content_end + 4 > len) break; /* unterminated, bail */

				for (k = content_start; k < content_end; k++) {
					if (s[k] == '\n') { first_line_end = k; break; }
				}
				if (first_line_end == (size_t) -1) break; /* single line: not a term+def shape */

				if (php_mdhtml_is_colon_line(s + content_start, first_line_end - content_start)) {
					break; /* the "term" line can't itself be a colon-line */
				}

				{
					size_t pos = first_line_end + 1;
					int all_rest_colon = (pos < content_end);
					while (pos < content_end) {
						size_t le = pos;
						while (le < content_end && s[le] != '\n') le++;
						if (!php_mdhtml_is_colon_line(s + pos, le - pos)) { all_rest_colon = 0; break; }
						pos = le + 1;
					}
					if (!all_rest_colon) break;
				}

				smart_str_appends(&dl_body, "  <dt>");
				smart_str_appendl(&dl_body, s + content_start, first_line_end - content_start);
				smart_str_appends(&dl_body, "</dt>\n");
				{
					size_t pos = first_line_end + 1;
					while (pos < content_end) {
						size_t le = pos;
						const char *dd;
						size_t dd_len;
						while (le < content_end && s[le] != '\n') le++;
						php_mdhtml_colon_line_content(s + pos, le - pos, &dd, &dd_len);
						smart_str_appends(&dl_body, "  <dd>");
						smart_str_appendl(&dl_body, dd, dd_len);
						smart_str_appends(&dl_body, "</dd>\n");
						pos = le + 1;
					}
				}

				matched_any = 1;
				after_all = content_end + 4;

				scan = after_all;
				while (scan < len && (s[scan] == '\n' || s[scan] == ' ' || s[scan] == '\t' || s[scan] == '\r')) scan++;
			}

			if (matched_any) {
				smart_str_appends(&out, "<dl>\n");
				smart_str_append(&out, dl_body.s);
				smart_str_appends(&out, "</dl>");
				smart_str_free(&dl_body);
				i = after_all;
				continue;
			}
			smart_str_free(&dl_body);
		}

		smart_str_appendc(&out, s[i]);
		i++;
	}

	smart_str_0(&out);
	return out.s ? out.s : ZSTR_EMPTY_ALLOC();
}

/* ========================================================================
 * PLUGIN SYSTEM ({% name args %} inline, {% name args\nbody\n%} block)
 *
 * Ported from MD::'s plugin system (md.class.php STEP 2), but the
 * extraction happens on the raw Markdown *string*, exactly like MD::'s
 * own STEP 2 does, and for the same reason: at this point nothing has
 * been parsed yet, so there is no tree to hook a real cmark_syntax_extension
 * into (and writing one would still need this same callback-into-PHP
 * machinery for the plugin body). A registered plugin's PHP callback is
 * called synchronously (call_user_function()) with ($args, $body) as
 * MD::registerPlugin() documents, its return value is stashed, and the
 * matched span is replaced with a placeholder (the same STX/ETX control-
 * byte convention MD:: uses -- bytes with no Markdown meaning, guaranteed
 * to survive cmark's own text escaping untouched) for cmark to parse
 * around. After rendering, php_mdhtml_inject_plugins() swaps each
 * placeholder for its real output.
 *
 * KNOWN LIMITATION (unlike MD::, which explicitly protects against this):
 * a `{% %}` tag written *inside* a code span/block in the source is not
 * detected as such here except for ``` fenced blocks (tracked via a
 * simple "does this line start with ```" toggle) -- an inline single-
 * backtick code span containing literal `{% %}` text is not excluded and
 * would still be treated as a live plugin invocation. Revisit if this
 * turns out to matter in practice (documenting Kirigami's own plugin
 * syntax in a single-backtick code span, for instance).
 * ======================================================================== */

typedef struct {
	zend_string *placeholder;
	zend_string *output;
} php_mdhtml_plugin_output;

typedef struct {
	php_mdhtml_plugin_output *items;
	size_t count;
	size_t capacity;
} php_mdhtml_plugin_outputs;

static void php_mdhtml_plugin_outputs_push(php_mdhtml_plugin_outputs *list, zend_string *placeholder, zend_string *output) {
	if (list->count == list->capacity) {
		list->capacity = list->capacity ? list->capacity * 2 : 8;
		list->items = erealloc(list->items, list->capacity * sizeof(php_mdhtml_plugin_output));
	}
	list->items[list->count].placeholder = placeholder;
	list->items[list->count].output = output;
	list->count++;
}

static void php_mdhtml_plugin_outputs_destroy(php_mdhtml_plugin_outputs *list) {
	size_t i;
	for (i = 0; i < list->count; i++) {
		zend_string_release(list->items[i].placeholder);
		zend_string_release(list->items[i].output);
	}
	if (list->items) {
		efree(list->items);
	}
}

/* Tokenizes a plugin's inline argument string the same way MD::'s
 * $parseArgs closure does: bare words, "double quoted" and 'single
 * quoted' (both supporting backslash escapes, stripped the same way
 * PHP's stripslashes() would). */
static void php_mdhtml_parse_plugin_args(const char *raw, size_t len, zval *args_array) {
	size_t i = 0;
	array_init(args_array);

	while (i < len) {
		while (i < len && isspace((unsigned char) raw[i])) i++;
		if (i >= len) break;

		if (raw[i] == '"' || raw[i] == '\'') {
			char quote = raw[i];
			smart_str tok = {0};
			i++;
			while (i < len && raw[i] != quote) {
				if (raw[i] == '\\' && i + 1 < len) {
					smart_str_appendc(&tok, raw[i + 1]);
					i += 2;
					continue;
				}
				smart_str_appendc(&tok, raw[i]);
				i++;
			}
			if (i < len) i++; /* past closing quote */
			smart_str_0(&tok);
			add_next_index_str(args_array, tok.s ? tok.s : ZSTR_EMPTY_ALLOC());
		} else {
			size_t start = i;
			while (i < len && !isspace((unsigned char) raw[i])) i++;
			add_next_index_stringl(args_array, raw + start, i - start);
		}
	}
}

static size_t php_mdhtml_trim_len(const char *s, size_t len) {
	while (len > 0 && isspace((unsigned char) s[len - 1])) len--;
	return len;
}
static size_t php_mdhtml_ltrim_start(const char *s, size_t len) {
	size_t i = 0;
	while (i < len && isspace((unsigned char) s[i])) i++;
	return i;
}

/*
 * Scans `md` for `{% name args %}` / `{% name args\nbody\n%}` spans,
 * calling each registered plugin's PHP callback and replacing the span
 * with a placeholder token; unregistered names are left completely
 * untouched (cmark's own text-node HTML-escaping already makes a bare
 * `{% unknown %}` safe to render as literal text, so unlike MD:: there is
 * no need to pre-escape it here). Returns the rewritten Markdown as a new
 * zend_string; `outputs` collects each placeholder's real HTML for
 * php_mdhtml_inject_plugins() to substitute back in after rendering.
 */
static zend_string *php_mdhtml_extract_plugins(const char *md, size_t len, php_mdhtml_plugin_outputs *outputs) {
	smart_str out = {0};
	size_t i = 0;
	size_t line_start = 0;
	int in_fence = 0;

	while (i < len) {
		if (i == line_start && i + 3 <= len && md[i] == '`' && md[i + 1] == '`' && md[i + 2] == '`') {
			in_fence = !in_fence;
		}

		if (!in_fence && i + 1 < len && md[i] == '{' && md[i + 1] == '%') {
			size_t p = i + 2;
			size_t name_start, name_len;
			while (p < len && isspace((unsigned char) md[p])) p++;
			name_start = p;
			while (p < len && (isalnum((unsigned char) md[p]) || md[p] == '_' || md[p] == '-')) p++;
			name_len = p - name_start;

			if (name_len > 0) {
				/* first '%}' from p onward is the tag's true end (matches
				 * the non-greedy body group in MD::'s regex, which always
				 * stops at the first "%}" it finds); a '\n' encountered
				 * strictly before that close makes this block form. */
				size_t k, first_newline = (size_t) -1, close = (size_t) -1;
				for (k = p; k < len; k++) {
					if (md[k] == '\n' && first_newline == (size_t) -1) {
						first_newline = k;
					}
					if (k + 1 < len && md[k] == '%' && md[k + 1] == '}') {
						close = k;
						break;
					}
				}

				if (close != (size_t) -1) {
					size_t args_start = p, args_end;
					const char *body_ptr = NULL;
					size_t body_len = 0;
					size_t match_end = close + 2;
					size_t j;

					if (first_newline != (size_t) -1 && first_newline < close) {
						args_end = first_newline;
						{
							size_t bs = first_newline + 1;
							size_t be = close;
							bs += php_mdhtml_ltrim_start(md + bs, be - bs);
							be = bs + php_mdhtml_trim_len(md + bs, be - bs);
							body_ptr = md + bs;
							body_len = be - bs;
						}
					} else {
						args_end = close;
					}
					args_end = args_start + php_mdhtml_trim_len(md + args_start, args_end - args_start);

					{
						zend_string *key = zend_string_init(md + name_start, name_len, 0);
						zend_str_tolower(ZSTR_VAL(key), ZSTR_LEN(key));
						zval *callback = zend_hash_find(&MDHTML_G(plugins), key);
						zend_string_release(key);

						if (callback) {
							zval args_array, body_zv, retval;
							zval params[2];

							php_mdhtml_parse_plugin_args(md + args_start, args_end - args_start, &args_array);
							if (body_ptr) {
								ZVAL_STRINGL(&body_zv, body_ptr, body_len);
							} else {
								ZVAL_EMPTY_STRING(&body_zv);
							}
							ZVAL_COPY_VALUE(&params[0], &args_array);
							ZVAL_COPY_VALUE(&params[1], &body_zv);

							ZVAL_UNDEF(&retval);
							if (call_user_function(NULL, NULL, callback, &retval, 2, params) == SUCCESS
								&& !Z_ISUNDEF(retval)) {
								zend_string *output = zval_get_string(&retval);
								zend_string *placeholder;
								smart_str ph = {0};
								smart_str_appends(&ph, "\x02PLG");
								smart_str_append_long(&ph, (zend_long) outputs->count);
								smart_str_appends(&ph, "\x03");
								smart_str_0(&ph);
								placeholder = ph.s;

								smart_str_append(&out, placeholder);
								php_mdhtml_plugin_outputs_push(outputs, zend_string_copy(placeholder), output);
							}
							zval_ptr_dtor(&retval);
							zval_ptr_dtor(&args_array);
							zval_ptr_dtor(&body_zv);

							/* re-derive line_start/in_fence across the
							 * consumed span (best-effort -- see the
							 * KNOWN LIMITATION note above the section). */
							for (j = i; j < match_end; j++) {
								if (md[j] == '\n') line_start = j + 1;
							}
							i = match_end;
							continue;
						}
					}

					/* unregistered plugin name: leave the whole span
					 * untouched, verbatim. */
					smart_str_appendl(&out, md + i, match_end - i);
					for (j = i; j < match_end; j++) {
						if (md[j] == '\n') line_start = j + 1;
					}
					i = match_end;
					continue;
				}
			}
		}

		if (md[i] == '\n') {
			line_start = i + 1;
		}
		smart_str_appendc(&out, md[i]);
		i++;
	}

	smart_str_0(&out);
	return out.s ? out.s : ZSTR_EMPTY_ALLOC();
}

/* Substitutes each plugin placeholder for its real output in the final
 * rendered HTML. A placeholder that ended up alone inside its own
 * `<p>...</p>` (the common case for a block-style plugin used on its own
 * line) has that wrapping `<p>`/`</p>` removed first -- otherwise a
 * plugin returning block-level HTML (a `<div>`, an `<iframe>`, ...) would
 * end up illegally nested inside a `<p>`, exactly the case MD::'s own
 * STEP 13 paragraph-wrapping logic special-cases its `\x02PLG` marker
 * for. */
static zend_string *php_mdhtml_inject_plugins(zend_string *html, php_mdhtml_plugin_outputs *outputs) {
	zend_string *current = zend_string_copy(html);
	size_t i;

	for (i = 0; i < outputs->count; i++) {
		zend_string *placeholder = outputs->items[i].placeholder;
		zend_string *output = outputs->items[i].output;
		smart_str out = {0};
		const char *s = ZSTR_VAL(current);
		size_t len = ZSTR_LEN(current);
		size_t plen = ZSTR_LEN(placeholder);
		size_t pos = 0;
		int found_any = 0;

		while (pos < len) {
			if (pos + plen <= len && memcmp(s + pos, ZSTR_VAL(placeholder), plen) == 0) {
				if (out.s && ZSTR_LEN(out.s) >= 3 && memcmp(ZSTR_VAL(out.s) + ZSTR_LEN(out.s) - 3, "<p>", 3) == 0
					&& pos + plen + 4 <= len && memcmp(s + pos + plen, "</p>", 4) == 0) {
					ZSTR_LEN(out.s) -= 3; /* drop the "<p>" we already wrote */
					smart_str_append(&out, output);
					pos += plen + 4; /* also skip the matching "</p>" */
				} else {
					smart_str_append(&out, output);
					pos += plen;
				}
				found_any = 1;
				continue;
			}
			smart_str_appendc(&out, s[pos]);
			pos++;
		}

		if (found_any) {
			smart_str_0(&out);
			zend_string_release(current);
			current = out.s;
		} else {
			smart_str_free(&out);
		}
	}

	return current;
}

/* ========================================================================
 * PHP-FACING FUNCTIONS
 * ======================================================================== */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mdhtml_render, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, markdown, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const char *php_mdhtml_gfm_extensions[] = {
	"table", "strikethrough", "autolink", "tagfilter", "tasklist", NULL
};

/*
 * Covers the CommonMark+GFM structural core plus everything that could be
 * done as tree/HTML transforms (or, for plugins, a synchronous PHP
 * callback round-trip) without leaving MD::toHtml() itself to do it:
 * heading ids incl. the `{#id}` override syntax, emoji shortcodes, the
 * raw-HTML whitelist, external-link/image/task-list attribute shaping,
 * the `{% plugin %}` system (`MDHtml\RegisterPlugin()`), GFM-style
 * `> [!NOTE]` alerts, definition lists, and the
 * `==highlight==`/`^sup^`/`~sub~` extended inline syntax.
 */
PHP_FUNCTION(mdhtml_render)
{
	zend_string *markdown;
	zend_string *preprocessed;
	php_mdhtml_plugin_outputs plugin_outputs = {0};
	cmark_parser *parser;
	cmark_node *document;
	cmark_llist *extensions;
	char *html;
	zend_string *result;
	php_mdhtml_id_list heading_ids = {0};
	/* CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE: without it, cmark-gfm's own
	 * strikethrough extension accepts a *single* ~tilde~ as strikethrough
	 * too, which would greedily consume "H~2~O" during parsing before the
	 * subscript pass below ever sees a literal '~' -- found by actually
	 * testing subscript against strikethrough and getting <del>, not
	 * <sub>. Restricting strikethrough to ~~double~~ tildes (matching
	 * every real GFM implementation's actual behavior, e.g. GitHub's own)
	 * leaves single tildes for MDHtml's own subscript syntax. */
	int options = CMARK_OPT_UNSAFE | CMARK_OPT_FOOTNOTES | CMARK_OPT_STRIKETHROUGH_DOUBLE_TILDE;
	int i;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(markdown)
	ZEND_PARSE_PARAMETERS_END();

	preprocessed = zend_hash_num_elements(&MDHTML_G(plugins)) > 0
		? php_mdhtml_extract_plugins(ZSTR_VAL(markdown), ZSTR_LEN(markdown), &plugin_outputs)
		: zend_string_copy(markdown);

	parser = cmark_parser_new(options);

	for (i = 0; php_mdhtml_gfm_extensions[i]; i++) {
		cmark_syntax_extension *ext = cmark_find_syntax_extension(php_mdhtml_gfm_extensions[i]);
		if (ext) {
			cmark_parser_attach_syntax_extension(parser, ext);
		}
	}

	cmark_parser_feed(parser, ZSTR_VAL(preprocessed), ZSTR_LEN(preprocessed));
	document = cmark_parser_finish(parser);
	zend_string_release(preprocessed);
	extensions = cmark_parser_get_syntax_extensions(parser);

	php_mdhtml_sanitize_raw_html_nodes(document);
	php_mdhtml_process_emoji(document);
	php_mdhtml_apply_delim_pass(document, "==", 2, "<mark>", "</mark>");
	php_mdhtml_apply_delim_pass(document, "^", 1, "<sup>", "</sup>");
	php_mdhtml_apply_delim_pass(document, "~", 1, "<sub>", "</sub>");
	php_mdhtml_compute_heading_ids(document, &heading_ids);
	php_mdhtml_process_alerts(document, options, extensions);

	html = cmark_render_html(document, options, extensions);

	cmark_node_free(document);
	cmark_parser_free(parser);

	if (!html) {
		php_mdhtml_id_list_destroy(&heading_ids);
		php_mdhtml_plugin_outputs_destroy(&plugin_outputs);
		RETURN_EMPTY_STRING();
	}

	result = php_mdhtml_postprocess(html, strlen(html), &heading_ids);
	free(html); /* libc allocator -- see cmark_parser_new() vs _with_mem() */
	php_mdhtml_id_list_destroy(&heading_ids);

	{
		zend_string *with_dl = php_mdhtml_process_definition_lists(result);
		zend_string_release(result);
		result = with_dl;
	}

	if (plugin_outputs.count > 0) {
		zend_string *injected = php_mdhtml_inject_plugins(result, &plugin_outputs);
		zend_string_release(result);
		result = injected;
	}
	php_mdhtml_plugin_outputs_destroy(&plugin_outputs);

	RETVAL_STR(result);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mdhtml_register_emoji, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, shortcode, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, char, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* MDHtml\RegisterEmoji('shortcode', '<unicode char>') -- request-scoped,
 * mirrors MD::registerEmoji(). Overrides a builtin shortcode of the same
 * name for the rest of the request, same precedence as MD::emojiFor(). */
PHP_FUNCTION(mdhtml_register_emoji)
{
	zend_string *shortcode, *ch;
	zend_string *key;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_STR(shortcode)
		Z_PARAM_STR(ch)
	ZEND_PARSE_PARAMETERS_END();

	key = zend_string_init(ZSTR_VAL(shortcode), ZSTR_LEN(shortcode), 0);
	zend_str_tolower(ZSTR_VAL(key), ZSTR_LEN(key));

	zend_hash_update_ptr(&MDHTML_G(emoji_custom), key, zend_string_copy(ch));
	zend_string_release(key);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mdhtml_register_plugin, 0, 2, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, callback, IS_CALLABLE, 0)
ZEND_END_ARG_INFO()

/* MDHtml\RegisterPlugin('name', $callback) -- request-scoped, mirrors
 * MD::registerPlugin(). $callback is called as ($args, $body) exactly
 * like MD:: documents; stored as a plain zval (not a zend_fcall_info/
 * cache pair) so it can be looked up and invoked later, well after this
 * function's own call frame is gone. */
PHP_FUNCTION(mdhtml_register_plugin)
{
	zend_string *name;
	zval *callback;
	zend_string *key;
	zval callback_copy;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_STR(name)
		Z_PARAM_ZVAL(callback)
	ZEND_PARSE_PARAMETERS_END();

	if (!zend_is_callable(callback, 0, NULL)) {
		zend_argument_type_error(2, "must be a valid callback");
		RETURN_THROWS();
	}

	key = zend_string_init(ZSTR_VAL(name), ZSTR_LEN(name), 0);
	zend_str_tolower(ZSTR_VAL(key), ZSTR_LEN(key));

	ZVAL_COPY(&callback_copy, callback);
	zend_hash_update(&MDHTML_G(plugins), key, &callback_copy);
	zend_string_release(key);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mdhtml_unregister_plugin, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* MDHtml\UnregisterPlugin('name') -- mirrors MD::unregisterPlugin(). */
PHP_FUNCTION(mdhtml_unregister_plugin)
{
	zend_string *name;
	zend_string *key;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();

	key = zend_string_init(ZSTR_VAL(name), ZSTR_LEN(name), 0);
	zend_str_tolower(ZSTR_VAL(key), ZSTR_LEN(key));
	zend_hash_del(&MDHTML_G(plugins), key);
	zend_string_release(key);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_mdhtml_get_registered_plugins, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

/* MDHtml\GetRegisteredPlugins(): array -- mirrors
 * MD::getRegisteredPlugins(). */
PHP_FUNCTION(mdhtml_get_registered_plugins)
{
	zend_string *key;

	ZEND_PARSE_PARAMETERS_NONE();

	array_init(return_value);
	ZEND_HASH_FOREACH_STR_KEY(&MDHTML_G(plugins), key) {
		add_next_index_str(return_value, zend_string_copy(key));
	} ZEND_HASH_FOREACH_END();
}

static const zend_function_entry mdhtml_functions[] = {
	ZEND_NS_NAMED_FE("MDHtml", Render, PHP_FN(mdhtml_render), arginfo_mdhtml_render)
	ZEND_NS_NAMED_FE("MDHtml", RegisterEmoji, PHP_FN(mdhtml_register_emoji), arginfo_mdhtml_register_emoji)
	ZEND_NS_NAMED_FE("MDHtml", RegisterPlugin, PHP_FN(mdhtml_register_plugin), arginfo_mdhtml_register_plugin)
	ZEND_NS_NAMED_FE("MDHtml", UnregisterPlugin, PHP_FN(mdhtml_unregister_plugin), arginfo_mdhtml_unregister_plugin)
	ZEND_NS_NAMED_FE("MDHtml", GetRegisteredPlugins, PHP_FN(mdhtml_get_registered_plugins), arginfo_mdhtml_get_registered_plugins)
	PHP_FE_END
};

static void php_mdhtml_emoji_dtor(zval *zv) {
	zend_string_release((zend_string *) Z_PTR_P(zv));
}

PHP_MINIT_FUNCTION(mdhtml)
{
	size_t i;

	cmark_gfm_core_extensions_ensure_registered();

	zend_hash_init(&MDHTML_G(emoji_builtin), PHP_MDHTML_BUILTIN_EMOJI_COUNT, NULL, php_mdhtml_emoji_dtor, 1);
	for (i = 0; i < PHP_MDHTML_BUILTIN_EMOJI_COUNT; i++) {
		zend_string *val = zend_string_init(php_mdhtml_builtin_emoji[i].utf8, strlen(php_mdhtml_builtin_emoji[i].utf8), 1);
		zend_hash_str_update_ptr(&MDHTML_G(emoji_builtin), php_mdhtml_builtin_emoji[i].name, strlen(php_mdhtml_builtin_emoji[i].name), val);
	}

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(mdhtml)
{
	zend_hash_destroy(&MDHTML_G(emoji_builtin));
	return SUCCESS;
}

PHP_RINIT_FUNCTION(mdhtml)
{
	zend_hash_init(&MDHTML_G(emoji_custom), 8, NULL, php_mdhtml_emoji_dtor, 0);
	zend_hash_init(&MDHTML_G(plugins), 8, NULL, ZVAL_PTR_DTOR, 0);
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(mdhtml)
{
	zend_hash_destroy(&MDHTML_G(emoji_custom));
	zend_hash_destroy(&MDHTML_G(plugins));
	return SUCCESS;
}

PHP_MINFO_FUNCTION(mdhtml)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "mdhtml support", "enabled");
	php_info_print_table_row(2, "version", PHP_MDHTML_VERSION);
	php_info_print_table_row(2, "cmark-gfm version", CMARK_GFM_VERSION_STRING);
	php_info_print_table_end();
}

zend_module_entry mdhtml_module_entry = {
	STANDARD_MODULE_HEADER,
	"mdhtml",
	mdhtml_functions,
	PHP_MINIT(mdhtml),
	PHP_MSHUTDOWN(mdhtml),
	PHP_RINIT(mdhtml),
	PHP_RSHUTDOWN(mdhtml),
	PHP_MINFO(mdhtml),
	PHP_MDHTML_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MDHTML
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(mdhtml)
#endif
