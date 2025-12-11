import type { SxProps, Theme } from '@mui/material/styles';

/**
 * Shared hover effect styling for interactive cards.
 * Provides consistent lift-on-hover animation across the app.
 */
export const hoverCardSx: SxProps<Theme> = {
  transition: 'transform 0.2s, box-shadow 0.2s',
  cursor: 'pointer',
  '&:hover': {
    transform: 'translateY(-4px)',
    boxShadow: (theme) => theme.shadows[8],
  },
};

/**
 * Generates sx props for featured metric cards (AQI, IAQ, Comfort).
 * Includes color-tinted gradient background, border, and hover effects.
 *
 * @param colorVar - CSS color variable or value for the card's accent color
 * @returns SxProps for the Card component
 */
export const featuredCardSx = (colorVar: string): SxProps<Theme> => ({
  height: '100%',
  minHeight: 280,
  background: `linear-gradient(135deg, color-mix(in srgb, ${colorVar} 15%, transparent) 0%, color-mix(in srgb, ${colorVar} 5%, transparent) 100%)`,
  border: `2px solid color-mix(in srgb, ${colorVar} 25%, transparent)`,
  ...hoverCardSx,
});
