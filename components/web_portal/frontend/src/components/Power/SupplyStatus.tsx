import { useAtomValue } from 'jotai';
import Box from '@mui/material/Box';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Chip from '@mui/material/Chip';
import Grid from '@mui/material/Grid';
import Skeleton from '@mui/material/Skeleton';
import Typography from '@mui/material/Typography';
import {
  Check as CheckIcon,
  Close as CloseIcon,
  ElectricBolt as VoltageIcon,
  ElectricalServices as CurrentIcon,
  Tune as MaintainIcon,
} from '@mui/icons-material';
import { supplyDisplayAtom } from '../../store/atoms';

/**
 * Supply status card displaying external power supply information
 */
export function SupplyStatus() {
  const supply = useAtomValue(supplyDisplayAtom);

  if (!supply) {
    return (
      <Card sx={{ height: '100%' }}>
        <CardContent>
          <Typography variant="h6" gutterBottom>
            Supply Status
          </Typography>

          {/* Status chip skeleton */}
          <Box sx={{ mb: 2 }}>
            <Skeleton variant="rounded" width={120} height={32} />
          </Box>

          {/* Metrics skeleton */}
          <Grid container spacing={2}>
            {[...Array(3)].map((_, i) => (
              <Grid size={{ xs: 12 }} key={i}>
                <Box display="flex" alignItems="center" gap={1}>
                  <Skeleton variant="circular" width={20} height={20} />
                  <Box>
                    <Skeleton variant="text" width={80} height={16} />
                    <Skeleton variant="text" width={100} height={20} />
                  </Box>
                </Box>
              </Grid>
            ))}
          </Grid>
        </CardContent>
      </Card>
    );
  }

  return (
    <Card sx={{ height: '100%' }}>
      <CardContent>
        <Box display="flex" alignItems="center" justifyContent="space-between" sx={{ mb: 2 }}>
          <Typography variant="h6">
            Supply Status
          </Typography>
          <Chip
            icon={supply.supply_good ? <CheckIcon /> : <CloseIcon />}
            label={supply.supply_good ? 'Good' : 'Not Connected'}
            color={supply.supply_good ? 'success' : 'default'}
            variant={supply.supply_good ? 'filled' : 'outlined'}
            size="small"
          />
        </Box>

        <Grid container spacing={2}>
          {/* Supply Voltage */}
          <Grid size={4}>
            <Box display="flex" alignItems="center" gap={1}>
              <VoltageIcon fontSize="small" color="action" />
              <Box>
                <Typography variant="caption" color="text.secondary">
                  Voltage
                </Typography>
                <Typography variant="body2" fontWeight={500}>
                  {(supply.supply_mv / 1000).toFixed(2)}V
                </Typography>
              </Box>
            </Box>
          </Grid>

          {/* Supply Current */}
          <Grid size={4}>
            <Box display="flex" alignItems="center" gap={1}>
              <CurrentIcon fontSize="small" color="action" />
              <Box>
                <Typography variant="caption" color="text.secondary">
                  Current
                </Typography>
                <Typography variant="body2" fontWeight={500}>
                  {supply.supply_ma}mA
                </Typography>
              </Box>
            </Box>
          </Grid>

          {/* Maintain Voltage (MPP) */}
          <Grid size={4}>
            <Box display="flex" alignItems="center" gap={1}>
              <MaintainIcon fontSize="small" color="action" />
              <Box>
                <Typography variant="caption" color="text.secondary">
                  Maintain (MPP)
                </Typography>
                <Typography variant="body2" fontWeight={500}>
                  {supply.maintain_mv}mV
                </Typography>
              </Box>
            </Box>
          </Grid>
        </Grid>
      </CardContent>
    </Card>
  );
}
