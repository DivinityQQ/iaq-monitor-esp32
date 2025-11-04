import Box from '@mui/material/Box';
import Stack from '@mui/material/Stack';
import Typography from '@mui/material/Typography';
import { memo, ReactNode } from 'react';

interface InfoSectionProps { title: string; children: ReactNode }
function InfoSectionBase({ title, children }: InfoSectionProps) {
  return (
    <Box>
      <Typography variant="overline" color="text.secondary" sx={{ fontWeight: 600 }}>
        {title}
      </Typography>
      <Stack spacing={0.5} sx={{ mt: 1 }}>
        {children}
      </Stack>
    </Box>
  );
}

interface InfoRowProps { label: string; value: string | number }
function InfoRowBase({ label, value }: InfoRowProps) {
  return (
    <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
      <Typography variant="body2" color="text.secondary">{label}</Typography>
      <Typography variant="body2" sx={{ fontWeight: 500 }}>{value}</Typography>
    </Box>
  );
}

export const InfoSection = memo(InfoSectionBase);
export const InfoRow = memo(InfoRowBase);

