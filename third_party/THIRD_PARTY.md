# Third-Party Dependencies

License audit per ADR-068. Every vendored dependency declares its **license**,
**provenance** and **why**; the CI job `license-audit` fails if anything under
`third_party/` is undeclared or a row is malformed. Dependencies are vendored
behind our own interfaces (ADR-061): we integrate, we never couple. Nothing is
fetched at build time (no FetchContent, no package managers, rule 05).

## Dependencies

| Dependency | Version | License | Provenance | SHA-256 | Why |
|---|---|---|---|---|---|
| doctest | 2.4.11 | MIT | https://github.com/doctest/doctest/releases/tag/v2.4.11 | 44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb | Testing framework for the harness (ADR-008): single header, MIT, works with `-fno-exceptions`; vendored behind our own interface (ADR-061) |

## License texts

- **doctest (MIT)** — the license text is embedded in the header
  (`third_party/doctest/doctest.h`), see also https://opensource.org/licenses/MIT
