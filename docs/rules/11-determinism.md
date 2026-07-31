# Regla 11 — Determinismo y Estado

> La simulación es reproducible o no es. Replay, red (server authority), tests y la IA
> dependen de esto — no es un lujo (ADR-013, ADR-038, ADR-056).

## Reglas duras

1. **No hidden state**: todo estado vive en el mundo/sistemas, nunca en globales
   mutables ni estáticos. Un objeto que muta fuera del mundo = bug (ADR-038).
2. **RNG por seed**: toda aleatoriedad usa un generador con seed explícito y
   reproducible; nunca `rand()` global ni time-based (ADR-013).
3. **Inputs/eventos como data**: la simulación avanza por inputs y comandos
   deterministas; grabar inputs = poder repetir el frame (ADR-033, ADR-039).
4. **Math determinista**: sin `-ffast-math`; política NaN/Inf explícita; el mismo
   código produce el mismo resultado en todas las plataformas (ADR-056).
5. **Orden declarado**: el scheduler usa read/write sets (ADR-018); un sistema que
   lee lo que otro escribe sin declararlo = bug.
6. **Replay es herramienta de debug**: si un bug no se reproduce con el mismo seed e
   inputs, el bug es del determinismo, no del repro.

## Alcance futuro (F15)

- En red, el server es la verdad del mundo; los clients predicen y rebobinan (ADR-062/096).
- La generación procedural es data: mismo seed → mismo mundo (ADR-076).

## Verificación

- Property tests (ADR-017): invariantes con entradas aleatorias y seed fijo.
- Replay de inputs: la grabación reproduce el frame exacto (ADR-013).
