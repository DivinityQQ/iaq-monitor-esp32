/**
 * Production-safe logger that no-ops in production builds
 * to avoid console overhead and information leakage
 */

const isProduction = import.meta.env.PROD;

export const logger = {
  log: isProduction ? (..._args: any[]) => {} : console.log.bind(console),
  error: isProduction ? (..._args: any[]) => {} : console.error.bind(console),
  warn: isProduction ? (..._args: any[]) => {} : console.warn.bind(console),
  info: isProduction ? (..._args: any[]) => {} : console.info.bind(console),
  debug: isProduction ? (..._args: any[]) => {} : console.debug.bind(console),
};
