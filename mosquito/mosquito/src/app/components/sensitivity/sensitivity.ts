import { Component, OnInit } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { MatSliderModule } from '@angular/material/slider';


@Component({
  selector: 'app-sensitivity',
  imports: [MatSliderModule, FormsModule],
  templateUrl: './sensitivity.html',
  styleUrl: './sensitivity.scss'
})
export class Sensitivity implements OnInit {
  sensitivityLevel: number = NaN;

  ngOnInit() {
    fetch('/sensitivity')
      .then(res => res.text())
      .then(val => {
        this.sensitivityLevel = Number(val);
      });
  }

  onSliderChange(event: any) {
    this.sensitivityLevel = event.target.value;
    fetch(`/sensitivity?value=${encodeURIComponent(this.sensitivityLevel)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'text/plain' }      
    });
  }
}
