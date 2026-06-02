echo "Generating ISR thingy doo-dads..."

index=0
otherIndex=32
endof=15

while [ "$index" -le "$endof" ]; do
    echo "  idt_set_gate($otherIndex, (uint32_t)irq$index);"
    ((index++))
    ((otherIndex++))
done
