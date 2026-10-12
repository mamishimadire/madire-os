# Contributing to Madire OS

Madire OS is licensed under the [Apache License 2.0](LICENSE); the name and the logo are not (see [TRADEMARKS.md](TRADEMARKS.md)). By contributing, you agree that your contribution is licensed under the Apache License 2.0.

## How work is done

Every change follows the workflow in [AGENTS.md](AGENTS.md): a **plan**, the owner's **review and approval**, then **small stages** with stop points and tests. Read [docs/PROGRESS.md](docs/PROGRESS.md) and AGENTS.md before you start, and raise design changes before you implement them.

## Developer Certificate of Origin (DCO)

Contributions are accepted under the [Developer Certificate of Origin 1.1](https://developercertificate.org). Every commit must be signed off:

```sh
git commit -s
```

This adds a `Signed-off-by: Your Name <your@email>` line, which certifies that you wrote the contribution, or otherwise have the right to submit it, under the project's licence. Commits without it are not accepted.

## Code origin

These rules are those of AGENTS.md, section 11:

- **Madire OS's own code is written from scratch.** Reference projects (with any licence, or none) may be read for ideas, architecture and design only; their code, text or assets are never copied or closely reproduced. How far each project may be read is set in [docs/references.md](docs/references.md).
- **Third-party libraries** are allowed only with a permissive licence (MIT, BSD, Apache 2.0, zlib, 0BSD, CC0-1.0), only with the owner's explicit approval per library, kept in `third_party/<name>/` with its licence file, and listed in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).
- **Never:** code with no licence; GPL, LGPL or APSL code; snippets from forums, blogs or tutorials.

## Asset origin

Images, fonts, sounds and other assets are, by default, only original work, CC0, public domain, CC-BY or SIL OFL. CC BY-SA assets need the owner's approval. Assets with no licence or of unknown origin are never used. `THIRD_PARTY_ASSETS.md` is created when the first third-party asset ships.

## Security issues

Don't report vulnerabilities in public; see [SECURITY.md](SECURITY.md).
