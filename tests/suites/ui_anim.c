#define _POSIX_C_SOURCE 200809L

/*
 * The animation core: the curves, the value in flight, and the table that keys one per control.
 *
 * All of it is arithmetic over a clock the test supplies, which is the whole reason it is a
 * module of its own rather than a few lines inside the switch widget - a slide is testable
 * here, frame by frame and to the permille, with no framebuffer anywhere near it.
 */

#include "framework/inkcell_test.h"

#include "inkcell/ui/anim.h"

INKCELL_TEST_CASE(anim_easings_pin_both_ends, unit) {
    for (int ease = 0; ease < INKCELL_EASE_COUNT; ++ease) {
        INKCELL_TEST_FAIL_IF(inkcell_ease((enum inkcell_ease)ease, 0) != 0,
                          "every curve should start at 0");
        INKCELL_TEST_FAIL_IF(inkcell_ease((enum inkcell_ease)ease, INKCELL_ANIM_ONE) !=
                              INKCELL_ANIM_ONE,
                          "every curve should land exactly on its target");
        /* Out of range is clamped rather than extrapolated: a clock that jumped must not put a
           knob outside its track. */
        INKCELL_TEST_FAIL_IF(inkcell_ease((enum inkcell_ease)ease, -500) != 0,
                          "a negative progress should clamp to 0");
        INKCELL_TEST_FAIL_IF(inkcell_ease((enum inkcell_ease)ease, 5 * INKCELL_ANIM_ONE) !=
                              INKCELL_ANIM_ONE,
                          "an overshooting progress should clamp to the end");
    }

    /* Ease-out covers most of the distance early - that is what "out" means, and it is what
       makes a control feel like it answered the press rather than thought about it. */
    const int32_t linear = inkcell_ease(INKCELL_EASE_LINEAR, INKCELL_ANIM_ONE / 4);
    const int32_t out = inkcell_ease(INKCELL_EASE_OUT, INKCELL_ANIM_ONE / 4);
    INKCELL_TEST_FAIL_IF(out <= linear, "ease-out should be ahead of linear a quarter of the way in");

    /* Ease-in-out is symmetric about the middle, and passes through it. */
    INKCELL_TEST_FAIL_IF(inkcell_ease(INKCELL_EASE_IN_OUT, INKCELL_ANIM_ONE / 2) !=
                          INKCELL_ANIM_ONE / 2,
                      "ease-in-out should cross the halfway point halfway through");
    const int32_t early = inkcell_ease(INKCELL_EASE_IN_OUT, INKCELL_ANIM_ONE / 5);
    const int32_t late = inkcell_ease(INKCELL_EASE_IN_OUT, 4 * INKCELL_ANIM_ONE / 5);
    INKCELL_TEST_FAIL_IF(early + late != INKCELL_ANIM_ONE, "ease-in-out should be symmetric");
    record_success(test_name);
}

INKCELL_TEST_CASE(anim_value_walks_from_start_to_target, unit) {
    struct inkcell_anim anim;
    inkcell_anim_set(&anim, 0);
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(&anim, 1000U) != 0, "a set value should sit where it is");
    INKCELL_TEST_FAIL_IF(inkcell_anim_active(&anim, 1000U), "nothing set should be in flight");

    inkcell_anim_to(&anim, 1000U, INKCELL_ANIM_ONE, 200U, INKCELL_EASE_LINEAR);
    INKCELL_TEST_FAIL_IF(!inkcell_anim_active(&anim, 1000U), "it should be in flight at the start");
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(&anim, 1000U) != 0, "it should start where it was");
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(&anim, 1100U) != INKCELL_ANIM_ONE / 2,
                      "linear should be halfway at half the duration");
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(&anim, 1200U) != INKCELL_ANIM_ONE,
                      "it should land exactly on its target");
    INKCELL_TEST_FAIL_IF(inkcell_anim_active(&anim, 1200U),
                      "it should be settled the moment the window closes");

    /* A clock that jumped a long way - a device that slept, a capture stepping time - lands on
       the target rather than running off past it. */
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(&anim, 900000U) != INKCELL_ANIM_ONE,
                      "a far-future clock should still read the target");
    record_success(test_name);
}

/*
 * The case that is easy to get wrong and obvious once it is wrong: flicking a switch twice in
 * quick succession. The knob has to turn round from where it actually is, not from the end it
 * originally left.
 */
INKCELL_TEST_CASE(anim_reversal_starts_from_where_it_got_to, unit) {
    struct inkcell_anim anim;
    inkcell_anim_set(&anim, 0);
    inkcell_anim_to(&anim, 1000U, INKCELL_ANIM_ONE, 200U, INKCELL_EASE_LINEAR);

    const int32_t midway = inkcell_anim_value(&anim, 1100U);
    INKCELL_TEST_FAIL_IF(midway != INKCELL_ANIM_ONE / 2, "half a duration in should be half way");

    inkcell_anim_to(&anim, 1100U, 0, 200U, INKCELL_EASE_LINEAR);
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(&anim, 1100U) != midway,
                      "a reversal should begin from where the knob actually is");
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(&anim, 1300U) != 0,
                      "and should still reach the new target");

    /* Re-aiming at the target it is already heading for changes nothing, so a widget may call
       this every frame with the state it can see. */
    inkcell_anim_to(&anim, 1000U, INKCELL_ANIM_ONE, 200U, INKCELL_EASE_LINEAR);
    const struct inkcell_anim before = anim;
    inkcell_anim_to(&anim, 1050U, INKCELL_ANIM_ONE, 200U, INKCELL_EASE_LINEAR);
    INKCELL_TEST_FAIL_IF(anim.start_ms != before.start_ms || anim.from != before.from,
                      "re-aiming at the current target should be a no-op");
    record_success(test_name);
}

/*
 * First sight of an id adopts the value; a change after that animates. This is what stops every
 * switch on a Settings section sliding in from off on the frame the screen opens.
 */
INKCELL_TEST_CASE(anim_table_adopts_then_animates, unit) {
    struct inkcell_anim_table table;
    inkcell_anim_table_reset(&table);

    const int32_t first =
        inkcell_anim_track(&table, 7U, 1000U, INKCELL_ANIM_ONE, 200U, INKCELL_EASE_LINEAR);
    INKCELL_TEST_FAIL_IF(first != INKCELL_ANIM_ONE, "a control should be drawn at its value on sight");
    INKCELL_TEST_FAIL_IF(inkcell_anim_table_active(&table, 1000U),
                      "a first paint should not leave anything moving");

    const int32_t flipped = inkcell_anim_track(&table, 7U, 1000U, 0, 200U, INKCELL_EASE_LINEAR);
    INKCELL_TEST_FAIL_IF(flipped != INKCELL_ANIM_ONE, "a change should start from the old value");
    INKCELL_TEST_FAIL_IF(!inkcell_anim_table_active(&table, 1000U),
                      "a change should ask for more frames");
    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 7U, 1100U, 0, 200U, INKCELL_EASE_LINEAR) !=
                          INKCELL_ANIM_ONE / 2,
                      "the table should carry the animation forward across frames");
    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 7U, 1200U, 0, 200U, INKCELL_EASE_LINEAR) != 0,
                      "and should land on the target");
    INKCELL_TEST_FAIL_IF(inkcell_anim_table_active(&table, 1200U),
                      "a settled table should stop asking for frames");

    /* Two ids are two controls: one moving must not report the other's position. */
    (void)inkcell_anim_track(&table, 8U, 1200U, INKCELL_ANIM_ONE, 200U, INKCELL_EASE_LINEAR);
    (void)inkcell_anim_track(&table, 8U, 1200U, 0, 200U, INKCELL_EASE_LINEAR);
    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 7U, 1300U, 0, 200U, INKCELL_EASE_LINEAR) != 0,
                      "one control moving should not disturb another");
    record_success(test_name);
}

/* An id of 0 means "nothing to key on": it draws correctly and never animates, rather than
   every anonymous control sharing one slot and dragging each other about. */
INKCELL_TEST_CASE(anim_table_ignores_the_null_id, unit) {
    struct inkcell_anim_table table;
    inkcell_anim_table_reset(&table);

    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 0U, 1000U, INKCELL_ANIM_ONE, 200U,
                                         INKCELL_EASE_OUT) != INKCELL_ANIM_ONE,
                      "an unkeyed control should draw at its value");
    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 0U, 1000U, 0, 200U, INKCELL_EASE_OUT) != 0,
                      "an unkeyed control should follow its value exactly");
    INKCELL_TEST_FAIL_IF(inkcell_anim_table_active(&table, 1000U),
                      "an unkeyed control should never ask for a frame");
    record_success(test_name);
}

/*
 * More controls than slots. The one that has been off screen longest gives up its slot, which
 * costs it the memory of a slide nobody was watching and nothing else - it re-seeds at its
 * current value the next time it is drawn.
 */
INKCELL_TEST_CASE(anim_table_evicts_the_least_recently_drawn, unit) {
    struct inkcell_anim_table table;
    inkcell_anim_table_reset(&table);

    uint64_t now = 1000U;
    for (uint32_t id = 1U; id <= INKCELL_ANIM_SLOTS; ++id, now += 10U) {
        (void)inkcell_anim_track(&table, id, now, 0, 200U, INKCELL_EASE_LINEAR);
    }

    /* Keep every slot but the first one warm, then ask for one more control than fits. */
    for (uint32_t id = 2U; id <= INKCELL_ANIM_SLOTS; ++id) {
        (void)inkcell_anim_track(&table, id, now, 0, 200U, INKCELL_EASE_LINEAR);
    }
    now += 10U;
    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 999U, now, INKCELL_ANIM_ONE, 200U,
                                         INKCELL_EASE_LINEAR) != INKCELL_ANIM_ONE,
                      "a control taking a reused slot should adopt its value, not slide to it");

    /* The evicted one is still drawable; it has simply forgotten where it was. */
    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 1U, now, INKCELL_ANIM_ONE, 200U,
                                         INKCELL_EASE_LINEAR) != INKCELL_ANIM_ONE,
                      "an evicted control should re-seed rather than misbehave");
    record_success(test_name);
}

/*
 * The loop: a value with no destination, which is what an indeterminate progress bar is made of.
 *
 * The two properties that matter are that it is a pure function of the clock - so a missed
 * frame costs nothing and a capture stepping time lands exactly where the arithmetic says - and
 * that it starts from its own beginning rather than from wherever the monotonic clock happened
 * to be when the widget appeared.
 */
INKCELL_TEST_CASE(anim_loop_runs_a_sawtooth_off_the_clock, unit) {
    struct inkcell_anim_table table;
    inkcell_anim_table_reset(&table);

    /* Whatever the clock reads on first sight, the loop is at its start. */
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 1U, 987654U, 1000U) != 0,
                      "a loop should begin at 0 whenever it is first drawn");
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 1U, 987654U + 250U, 1000U) != INKCELL_ANIM_ONE / 4,
                      "a quarter of the period in should be a quarter of the way along");
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 1U, 987654U + 750U, 1000U) !=
                          3 * INKCELL_ANIM_ONE / 4,
                      "three quarters in should be three quarters along");

    /* It wraps rather than stopping, and a clock that jumped a whole period lands where a clock
       that walked there would have. */
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 1U, 987654U + 1000U, 1000U) != 0,
                      "a full period should be back at the start");
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 1U, 987654U + 7250U, 1000U) != INKCELL_ANIM_ONE / 4,
                      "seven periods later should be exactly where one period later was");

    /* A loop is never "finished", so what keeps the repaint timer coming is that something is
       still drawing it - and what stops it is that nothing has for a beat. */
    INKCELL_TEST_FAIL_IF(!inkcell_anim_table_active(&table, 987654U + 7250U),
                      "a loop drawn this frame should be asking for the next one");
    INKCELL_TEST_FAIL_IF(
        !inkcell_anim_table_active(&table, 987654U + 7250U + INKCELL_ANIM_LOOP_STALE_MS),
        "a loop should survive right up to the stale window");
    INKCELL_TEST_FAIL_IF(
        inkcell_anim_table_active(&table, 987654U + 7250U + INKCELL_ANIM_LOOP_STALE_MS + 1U),
        "a loop nothing has drawn for a beat should stop asking for frames");

    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(NULL, 1U, 1000U, 1000U) != 0,
                      "a NULL table should read 0 rather than crash");
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 0U, 1000U, 1000U) != 0,
                      "an id of 0 is not a key, exactly as it is not for a transition");
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 1U, 1000U, 0U) != 0,
                      "a period of nothing should read 0 rather than divide by it");
    record_success(test_name);
}

/*
 * A control that stops looping and starts reporting a position - which is exactly what the
 * update meter does the moment the download learns the asset's size.
 *
 * It has to adopt the new value rather than transition to it: the sawtooth position it was
 * carrying was never a reading, so easing from it would slide the bar out of a number that
 * meant nothing into one that does.
 */
INKCELL_TEST_CASE(anim_loop_and_track_do_not_bleed_into_each_other, unit) {
    struct inkcell_anim_table table;
    inkcell_anim_table_reset(&table);

    (void)inkcell_anim_loop(&table, 7U, 1000U, 1000U);
    const int32_t mid_loop = inkcell_anim_loop(&table, 7U, 1600U, 1000U);
    INKCELL_TEST_FAIL_IF(mid_loop == 0, "the loop should be somewhere in its stride");

    INKCELL_TEST_FAIL_IF(inkcell_anim_track(&table, 7U, 1600U, INKCELL_ANIM_ONE / 2, 200U,
                                         INKCELL_EASE_OUT) != INKCELL_ANIM_ONE / 2,
                      "a loop turning into a reading should adopt it, not slide from a sawtooth");
    INKCELL_TEST_FAIL_IF(inkcell_anim_table_active(&table, 1600U + INKCELL_ANIM_LOOP_STALE_MS + 1U),
                      "the slot should stop asking for frames once it is no longer looping");

    /* And back the other way: a reading that becomes indeterminate restarts the loop from its
       beginning rather than resuming a stride it never had. */
    INKCELL_TEST_FAIL_IF(inkcell_anim_loop(&table, 7U, 5000U, 1000U) != 0,
                      "a reading turning back into a loop should start the loop at 0");
    record_success(test_name);
}

/* A NULL animation or table answers rather than crashing: the drawing code calls these on every
   frame and a guard at each call site is a guard somebody eventually forgets. */
INKCELL_TEST_CASE(anim_null_arguments_are_safe, unit) {
    INKCELL_TEST_FAIL_IF(inkcell_anim_value(NULL, 1000U) != 0, "a NULL animation should read 0");
    INKCELL_TEST_FAIL_IF(inkcell_anim_active(NULL, 1000U), "a NULL animation should not be active");
    INKCELL_TEST_FAIL_IF(inkcell_anim_table_active(NULL, 1000U), "a NULL table should not be active");
    INKCELL_TEST_FAIL_IF(inkcell_anim_track(NULL, 1U, 1000U, INKCELL_ANIM_ONE, 200U,
                                         INKCELL_EASE_OUT) != INKCELL_ANIM_ONE,
                      "a NULL table should still report the target");
    inkcell_anim_set(NULL, INKCELL_ANIM_ONE);
    inkcell_anim_to(NULL, 1000U, 0, 200U, INKCELL_EASE_OUT);
    inkcell_anim_table_reset(NULL);
    record_success(test_name);
}
