import { Component, OnInit } from '@angular/core';

@Component({
  selector: 'app-sensitivity',
  imports: [],
  templateUrl: './sensitivity.html',
  styleUrl: './sensitivity.scss'
})
export class Sensitivity implements OnInit {
  value: number = 0;

  ngOnInit() {
    fetch('/sensitivity')
      .then(res => res.text())
      .then(val => {
        this.value = Number(val);
      });
  }

  onSliderChange(event: any) {
    const newValue = event.target.value;
    this.value = newValue;
    fetch('/sensitivity', {
      method: 'PUT',
      headers: { 'Content-Type': 'text/plain' },
      body: String(newValue)
    });
  }
}
