# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Types of changes are:  Added, Changed, Deprecated, Removed, Fixed, Security.

## [0.1.0] - 2025-07-11

### Changed

- Project restructured:
    - Common code now built as a library
    - PAM daemon/callback as a sub-project that links against the library

### Added

- (PostgreSQL) New table and rule to track per-uid last-logged session timestamp
    - Existing event-logging tables move to a schema ("pam")
- Firewalling sub-project created
    - "firewall" schema


## [0.0.2] - 2025-06-05

### Added

- Added a MySQL database plugin
- Added a MySQL database schema example


## [0.0.1] - 2025-05-19

### Added

- Inception
