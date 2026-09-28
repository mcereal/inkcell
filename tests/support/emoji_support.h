#ifndef INKCELL_TEST_EMOJI_SUPPORT_H
#define INKCELL_TEST_EMOJI_SUPPORT_H

/*
 * Put the suite's emoji pack back in use: the built-in one, or - in a build with INKCELL_EMOJI
 * OFF - the same pack handed over at run time, as a program without one compiled in would.
 * A case that swaps the pack out calls this before it finishes, so the cases after it draw
 * with what every other case draws with. 0 or a negative errno.
 */
int inkcell_test_emoji_restore(void);

#endif /* INKCELL_TEST_EMOJI_SUPPORT_H */
