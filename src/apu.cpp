#include "apu.h"
#include "gbsystem.h"

#include <iostream>

#include "utils.h"

APU::APU(GBSystem* gb) {
    this->gb = gb;
}

void APU::tick() {
    uint8_t div = gb->address_space[DIV];
    bool div_apu_updated = false;

    if (div != last_div) {
        // Falling edge of bit 4 / 5 (dmg / cgb)

        uint8_t div_bit;
        if (gb->cgb()) {
            div_bit = 0x40;
        } else {
            div_bit = 0x20;
        }
        if (div % div_bit == 0) {
            div_apu++;
            div_apu_updated = true;
        }
        last_div = div;
    }

    handle_pulse_channel_1(div_apu_updated);
    handle_pulse_channel_2(div_apu_updated);
}

int16_t APU::current_sample() {
    int16_t sum = 0;
    sum += sample_pulse_channel_1();
    sum += sample_pulse_channel_2();
    // sum += sample_noise_channel();

    return sum;
}

void APU::handle_pulse_channel_1(bool div_apu_updated) {

    if (ch1_length >= 64) {
        return;
    }

    uint16_t period = gb->address_space[SND_P1_PER_HI] & 0b111;
    period <<= 8;
    period |= gb->address_space[SND_P1_PER_LOW];

    if (div_apu_updated) {
        uint8_t envelope_sweep_register = gb->address_space[SND_P1_VOL_ENV];
        uint8_t envelope_sweep_pace = (uint8_t) (envelope_sweep_register & 0b111);
        if (envelope_sweep_pace != 0 && div_apu % 8 == 0) {
            // Envelope Sweep
            if (ch1_envelope_timer-- == 0) {
                bool negate = (envelope_sweep_register & (1 << 3)) == 0;
                if (negate && ch1_volume == 0) {
                    ch1_volume--;
                } else if (!negate && ch1_volume != 15) {
                    ch1_volume++;
                }
                ch1_envelope_timer = envelope_sweep_pace;
            }
        }
        bool length_enable = (gb->address_space[SND_P1_PER_HI] & (1 << 6)) != 0;
        if (length_enable && div_apu % 2 == 0) {
            // Sound length
            if (++ch1_length >= 64) {
                ch1_volume = 0;
                ch1_length = 64;
                return;
            }
        }
        uint8_t freq_sweep_register = gb->address_space[SND_P1_SWEEP];
        uint8_t freq_sweep_pace = (freq_sweep_register & 0b1110000) >> 4;
        if (freq_sweep_pace != 0 && div_apu % 4 == 0) {
            // Frequency sweep

            if (ch1_sweep_timer-- == 0) {
                uint8_t step = freq_sweep_register & 0b111;
                bool negate = (freq_sweep_register & (1 << 3)) != 0;
                uint16_t new_frequency = period >> step;
                if (negate) {
                    period -= new_frequency;
                } else {
                    period += new_frequency;
                }

                gb->address_space[SND_P1_PER_HI] = (gb->address_space[SND_P1_PER_HI] & 0xF0) | (period >> 8);
                gb->address_space[SND_P1_PER_LOW] = period & 0xFF;

                if (period > 0x800) {
                    ch1_volume = 0;
                    ch1_length = 64;
                }
                ch1_sweep_timer = freq_sweep_pace;
            }
        }
    }

    if (--ch1_timer == 0) {
        ch1_pulse_index++;
        ch1_pulse_index %= 8;
        ch1_timer = (2048 - period) * 4;
    }
}

int16_t APU::sample_pulse_channel_1() {

    uint8_t duty_cycle = gb->address_space[SND_P1_LEN_DUTY] >> 6;

    float active = (PULSE_DUTY_CYCLES[duty_cycle] & (1 << ch1_pulse_index)) != 0;
    active -= 0.5f;
    active *= (ch1_volume / 15.0f);

    return (int16_t) (active * 32768 / 2);
}

void APU::handle_pulse_channel_2(bool div_apu_updated) {

    if (ch2_length >= 64) {
        return;
    }

    uint16_t period = gb->address_space[SND_P2_PER_HI] & 0b111;
    period <<= 8;
    period |= gb->address_space[SND_P2_PER_LOW];

    if (div_apu_updated) {
        uint8_t envelope_sweep_register = gb->address_space[SND_P2_VOL_ENV];
        uint8_t envelope_sweep_pace = (uint8_t) (envelope_sweep_register & 0b111);
        if (envelope_sweep_pace != 0 && div_apu % 8 == 0) {
            // Envelope Sweep
            if (ch2_envelope_timer-- == 0) {
                bool negate = (envelope_sweep_register & 0b1000) == 0;
                if (negate && ch2_volume == 0) {
                    ch2_volume--;
                } else if (!negate && ch2_volume != 15) {
                    ch2_volume++;
                }
                ch2_envelope_timer = envelope_sweep_pace;
            }
        }
        bool length_enable = (gb->address_space[SND_P2_PER_HI] & (1 << 6)) != 0;
        if (length_enable && div_apu % 2 == 0) {
            // Sound length
            std::cout << (int) ch2_length << std::endl;
            if (++ch2_length >= 64) {
                ch2_volume = 0;
                ch2_length = 64;
                return;
            }
        }
    }

    if (--ch2_timer == 0) {
        ch2_pulse_index++;
        ch2_pulse_index %= 8;
        ch2_timer = (2048 - period) * 4;
    }
}

int16_t APU::sample_pulse_channel_2() {
    uint8_t duty_cycle = gb->address_space[SND_P2_LEN_DUTY] >> 6;

    float active = (PULSE_DUTY_CYCLES[duty_cycle] & (1 << ch2_pulse_index)) != 0;
    active -= 0.5f;
    active *= (ch2_volume / 15.0f);

    return (int16_t) (active * 32768 / 2);
}

void APU::handle_noise_channel(bool div_apu_updated) {

    if (ch3_length >= 64) {
        return;
    }

    if (div_apu_updated) {
        // if (div_apu % 8 == 0) {
        //     // Envelope Sweep
        //     uint8_t envelope_register = gb->address_space[SND_P1_VOL_ENV];
        //     int8_t envelope = (int8_t) (envelope_register & 0b111);
        //     if (envelope != 0) {
        //         int8_t direction = ((envelope_register >> 3) & 1) * 2 - 1;
        //         if (!((ch1_volume == 0xF && direction == 1) || (ch1_volume == 0x0 && direction == -1))) {
        //             ch1_volume += direction;
        //         }
        //     }
        // }
        // bool length_enable = (gb->address_space[SND_P1_PER_HI] & (1 << 6)) != 0;
        // if (length_enable && div_apu % 2 == 0) {
        //     // Sound length
        //     if (++ch1_length == 64) {
        //         ch1_volume = 0;
        //         return;
        //     }
        // }
    }

    if (ch3_timer-- == 0) {
        uint8_t freq_random_register = gb->address_space[SND_NS_FREQ];
        ch3_shifted_value = ch3_lsfr & 1;

        bool update_value = ch3_shifted_value ^ ((ch3_lsfr & 0b10) >> 1);
        ch3_lsfr = utils::set_bit_value(ch3_lsfr, 15, update_value);
        if (freq_random_register & (1 << 3) != 0) {
            ch3_lsfr = utils::set_bit_value(ch3_lsfr, 7, update_value);
        }
        ch3_lsfr >>= 1;
    }
}

int16_t APU::sample_noise_channel() {
    float active = ch3_shifted_value * 2 - 1;
    active *= (ch3_volume / 15.0f);

    return (int16_t) (active * 4096);
}