# Ryu shortest binary64 conversion (internal dependency)

Source: https://github.com/ulfjack/ryu

Pinned revision: `4c0618b0e44f7ef027ebae05d2cc7812048f7c8f`.

Only the double-to-shortest implementation and its small-table dependencies
are included. Original copyright notices and both upstream licenses are
retained. Local changes only remove the `ryu/` prefix from quoted includes.

`abox_mqtt_number.c` compiles this source with `RYU_OPTIMIZE_SIZE` and
`RYU_ONLY_64_BIT_OPS`, then applies the JCS/ECMAScript formatting thresholds.
The buffered conversion does not allocate memory. Full-size tables, float,
fixed-precision and decimal-to-binary implementations are not included.
