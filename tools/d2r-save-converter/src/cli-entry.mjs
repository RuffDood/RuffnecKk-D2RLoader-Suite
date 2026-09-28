#!/usr/bin/env node

import { main } from './cli.mjs';

main().then((exitCode) => {
  process.exitCode = exitCode;
});
