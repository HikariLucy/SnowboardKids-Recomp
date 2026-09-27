# Accessibility capabilities and LIVE gate

The approved theme remains in place. Focus uses a thick outer border and filled
highlight; selected tabs and options also have a separate underline or weight
cue, so the state is visible beyond a color change. Screenshots of Controls,
Audio, Accessibility, Reset, and binding capture were captured locally for
mechanical review. Final aesthetic judgment remains pending human review.

**Reduced Motion** affects menu pulses, decorative spins, and smooth scrolling;
it does not change gameplay animation. It was toggled On in a race-active UI,
saved to `accessibility.json`, and loaded On by process B. Keyboard-only
navigation toggled it Off and back On. The setting's config and malformed-file
fallback tests pass. UI scale is not exposed because no validated backend exists.

Keyboard-only navigation reached Controls, Audio, Accessibility, and Resume.
A virtual SDL controller navigated the overlay and an Audio option. Physical
controller navigation and full accessibility compliance remain unverified.
