import constantDataModule from '@d2runewizard/d2s/lib/data/versions/105_constant_data.js';

import { augmentSaveReferenceIdentities } from './mod-schema.mjs';
import { VANILLA_EXCEL_TABLES } from './vanilla-excel.generated.mjs';

export const DEFAULT_D2R_V105_CONSTANTS = Object.freeze(
  augmentSaveReferenceIdentities(constantDataModule.constants, VANILLA_EXCEL_TABLES),
);
