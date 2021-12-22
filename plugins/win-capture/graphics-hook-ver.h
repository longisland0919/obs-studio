/* DO NOT MODIFY THIS FILE WITHOUT CONSULTING JIM.  OTHERWISE, JIM WILL DO
 * EVERYTHING IN HIS POWER TO MAKE YOUR LIFE MISERABLE FROM THEREON OUT WITH A
 * LEVEL OF DETERMINATION UNLIKE ANYTHING ANYONE HAS EVER SEEN IN THE HISTORY
 * OF MANKIND.
 *
 * YES, THAT MEANS YOU READING THIS RIGHT NOW.
 *
 * IF YOU HAVE A FORK AND FEEL YOU NEED TO MODIFY THIS, SUBMIT A PULL REQUEST
 * AND WAIT UNTIL IT HAS BEEN MERGED AND FULLY RELEASED IN THE CORE PROJECT
 * BEFORE USING IT.
 *
 * THIS IS YOUR ONLY WARNING. */

#define HOOK_VER_MAJOR 1
#define HOOK_VER_MINOR 7
#define HOOK_VER_PATCH 0

#define STRINGIFY(s) #s
#define MAKE_VERSION_NAME(major, minor, patch) \
	STRINGIFY(major) "." STRINGIFY(minor) "." STRINGIFY(patch) ".0"
#define HOOK_VERSION_NAME \
	MAKE_VERSION_NAME(HOOK_VER_MAJOR, HOOK_VER_MINOR, HOOK_VER_PATCH)
